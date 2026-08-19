/*
 * MintPRINT printer.device integration working driver path.
 *
 * Converts printer.device raster rows into a low-memory streaming document
 * (JPEG, PWG Raster, or PDF per Unit0's ENGINE= setting) and submits it to
 * the configured IPP Print-Job endpoint.
 *
 * Trace output: T:MintPRINT-driver.log
 * Debug JPEG:       T:MintPRINT-job.jpg
 * Debug PWG Raster: T:MintPRINT-job.pwg
 * Debug PDF:        T:MintPRINT-job.pdf
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <utility/tagitem.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <devices/prtgfx.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "config.h"
#include "jpeg_writer.h"
#include "pwg_writer.h"
#include "pdf_writer.h"
#include "ipp_client.h"
#include "spool.h"

struct ExecBase *SysBase = NULL;
struct DosLibrary *DOSBase = NULL;
struct PrinterData *PD = NULL;
struct PrinterExtendedData *PED = NULL;

extern struct PrinterExtendedData PEDData;

/*
 * OS 3.5+/3.2 extended printer-driver features:
 * - PRTA_NoIO: printer.device must not open parallel/serial transport.
 *   Tried removing this once to test a theory about printer.device's own
 *   NoIO code path; it made things worse (crashed WinUAE itself, not just
 *   the guest OS), so it stays on. See docs/DRIVER_SPOOL_PROCESS.md.
 * - PRTA_8BitGuns: request 8-bit Y/M/C/B intensity components, which is
 *   exactly what the future JPEG scanline backend wants.
 */
struct TagItem DriverTags[] = {
    { PRTA_NoIO,     TRUE },
    { PRTA_8BitGuns, TRUE },
    { TAG_DONE,      0 }
};

#define MP_JOB_FILE_JPEG ((CONST_STRPTR)"T:MintPRINT-job.jpg")
#define MP_JOB_FILE_PWG  ((CONST_STRPTR)"T:MintPRINT-job.pwg")
#define MP_JOB_FILE_PDF  ((CONST_STRPTR)"T:MintPRINT-job.pdf")

enum { MP_ENGINE_JPEG = 0, MP_ENGINE_PWG = 1, MP_ENGINE_PDF = 2 };

static ULONG g_page_width = 0;
static ULONG g_page_height = 0;
static ULONG g_rows_seen = 0;

static BOOL g_job_open = FALSE;
static UBYTE *g_rgb_row = NULL;
static ULONG g_rgb_row_bytes = 0;
static UBYTE *g_jpeg_scratch = NULL;
static ULONG g_jpeg_scratch_bytes = 0;
static UBYTE *g_pwg_scratch = NULL;
static ULONG g_pwg_scratch_bytes = 0;
static UBYTE *g_pdf_scratch = NULL;
static ULONG g_pdf_scratch_bytes = 0;
static ULONG g_job_rows_written = 0;
static BOOL g_job_failed = FALSE;
static int g_engine = MP_ENGINE_JPEG;
static MPJpegEncoder g_jpeg;
static MPPwgEncoder g_pwg;
static MPPdfEncoder g_pdf;
static struct MPConfig g_config;
static LONG g_config_source = MP_CONFIG_SOURCE_DEFAULTS;

/* cfg->engine is always exactly "jpeg", "pwg-raster", or "pdf" - config.c
 * only ever writes one of those three literal strings - so checking the
 * first two characters is enough to tell them apart (unlike checking just
 * the first character, which "pwg-raster" and "pdf" both start with). */
static int mp_detect_engine(const struct MPConfig *cfg)
{
    if (cfg->engine[0] == 'p' && cfg->engine[1] == 'w') return MP_ENGINE_PWG;
    if (cfg->engine[0] == 'p' && cfg->engine[1] == 'd') return MP_ENGINE_PDF;
    return MP_ENGINE_JPEG;
}

static CONST_STRPTR mp_job_filename(void)
{
    switch (g_engine) {
        case MP_ENGINE_PWG: return MP_JOB_FILE_PWG;
        case MP_ENGINE_PDF: return MP_JOB_FILE_PDF;
        default:            return MP_JOB_FILE_JPEG;
    }
}

static ULONG mp_strlen(const char *s)
{
    ULONG n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

/* Every log call below builds one line into this buffer, then hands it to
 * the spool process (spool.c) with a single mp_spool_log() call - never a
 * direct dos.library call from here. See spool.h for why. */
#define MP_LOG_LINE_MAX 256
static char g_log_line[MP_LOG_LINE_MAX];
static ULONG g_log_pos;

static void mp_log_reset(void)
{
    g_log_pos = 0;
    g_log_line[0] = 0;
}

static void mp_log_append(const char *s)
{
    ULONG len = mp_strlen(s);
    ULONG i;
    for (i = 0; i < len && g_log_pos + 1 < MP_LOG_LINE_MAX; ++i)
        g_log_line[g_log_pos++] = s[i];
    g_log_line[g_log_pos] = 0;
}

static void mp_log_append_long(LONG value)
{
    char buf[16];
    ULONG pos = sizeof(buf) - 1;
    ULONG v;

    buf[sizeof(buf) - 1] = 0;

    if (value < 0) {
        mp_log_append("-");
        v = (ULONG)(-value);
    } else {
        v = (ULONG)value;
    }

    if (v == 0) {
        mp_log_append("0");
        return;
    }

    while (v && pos) {
        buf[--pos] = (char)('0' + (v % 10));
        v /= 10;
    }
    mp_log_append(&buf[pos]);
}

static void mp_log_text(const char *event)
{
    mp_log_reset();
    mp_log_append("MintPRINT: ");
    mp_log_append(event);
    mp_spool_log(g_log_line);
}

static void mp_log_3(const char *event, LONG a, LONG b, LONG c)
{
    mp_log_reset();
    mp_log_append("MintPRINT: ");
    mp_log_append(event);
    mp_log_append(" ");
    mp_log_append_long(a);
    mp_log_append(" ");
    mp_log_append_long(b);
    mp_log_append(" ");
    mp_log_append_long(c);
    mp_spool_log(g_log_line);
}

static void mp_log_config(const struct MPConfig *cfg, LONG source)
{
    if (!cfg) return;

    mp_log_reset();
    mp_log_append("MintPRINT: Config source=");
    if (source == MP_CONFIG_SOURCE_ENV)
        mp_log_append("ENV");
    else if (source == MP_CONFIG_SOURCE_ENVARC)
        mp_log_append("ENVARC");
    else
        mp_log_append("defaults");
    mp_log_append(" host=");
    mp_log_append(cfg->host);
    mp_log_append(" port=");
    mp_log_append_long((LONG)cfg->port);
    mp_log_append(" path=");
    mp_log_append(cfg->path);
    mp_log_append(" keepjob=");
    mp_log_append_long(cfg->keep_job ? 1 : 0);
    if (cfg->media[0]) { mp_log_append(" media="); mp_log_append(cfg->media); }
    if (cfg->source[0]) { mp_log_append(" source="); mp_log_append(cfg->source); }
    if (cfg->color[0]) { mp_log_append(" color="); mp_log_append(cfg->color); }
    if (cfg->quality[0]) { mp_log_append(" quality="); mp_log_append(cfg->quality); }
    if (cfg->scaling[0]) { mp_log_append(" scaling="); mp_log_append(cfg->scaling); }
    if (cfg->sides[0]) { mp_log_append(" sides="); mp_log_append(cfg->sides); }
    mp_log_append(" engine=");
    mp_log_append(cfg->engine[0] ? cfg->engine : "jpeg");
    mp_spool_log(g_log_line);
}

static long mp_job_file_write(void *ctx, const unsigned char *data, unsigned long length)
{
    (void)ctx;
    return mp_spool_job_write((const UBYTE *)data, (ULONG)length) ? (long)length : -1;
}

static void mp_job_cleanup(void)
{
    if (g_job_open) {
        mp_spool_job_close();
        g_job_open = FALSE;
    }
    if (g_rgb_row) {
        FreeMem(g_rgb_row, g_rgb_row_bytes);
        g_rgb_row = NULL;
    }
    if (g_jpeg_scratch) {
        FreeMem(g_jpeg_scratch, g_jpeg_scratch_bytes);
        g_jpeg_scratch = NULL;
    }
    if (g_pwg_scratch) {
        FreeMem(g_pwg_scratch, g_pwg_scratch_bytes);
        g_pwg_scratch = NULL;
    }
    if (g_pdf_scratch) {
        FreeMem(g_pdf_scratch, g_pdf_scratch_bytes);
        g_pdf_scratch = NULL;
    }
    g_rgb_row_bytes = 0;
    g_jpeg_scratch_bytes = 0;
    g_pwg_scratch_bytes = 0;
    g_pdf_scratch_bytes = 0;
}

static BOOL mp_job_begin(ULONG width, ULONG height)
{
    ULONG need;

    mp_job_cleanup();
    g_job_rows_written = 0;
    g_job_failed = FALSE;
    g_engine = mp_detect_engine(&g_config);

    if (!DOSBase || width == 0 || height == 0 || width > 65535UL || height > 65535UL) {
        mp_log_text("Job begin rejected invalid dimensions");
        return FALSE;
    }

    g_rgb_row_bytes = width * 3UL;
    g_rgb_row = (UBYTE *)AllocMem(g_rgb_row_bytes, MEMF_PUBLIC);
    if (!g_rgb_row) {
        mp_log_text("RGB row allocation failed");
        mp_job_cleanup();
        return FALSE;
    }

    switch (g_engine) {
        case MP_ENGINE_PWG: need = mp_pwg_scratch_size(width); break;
        case MP_ENGINE_PDF: need = mp_pdf_scratch_size(width); break;
        default:            need = mp_jpeg_scratch_size(width); break;
    }
    if (!need) {
        mp_log_text("Encoder scratch size rejected width");
        mp_job_cleanup();
        return FALSE;
    }

    switch (g_engine) {
        case MP_ENGINE_PWG:
            g_pwg_scratch_bytes = need;
            g_pwg_scratch = (UBYTE *)AllocMem(need, MEMF_PUBLIC);
            if (!g_pwg_scratch) {
                mp_log_text("PWG scratch allocation failed");
                mp_job_cleanup();
                return FALSE;
            }
            break;
        case MP_ENGINE_PDF:
            g_pdf_scratch_bytes = need;
            g_pdf_scratch = (UBYTE *)AllocMem(need, MEMF_PUBLIC);
            if (!g_pdf_scratch) {
                mp_log_text("PDF scratch allocation failed");
                mp_job_cleanup();
                return FALSE;
            }
            break;
        default:
            g_jpeg_scratch_bytes = need;
            g_jpeg_scratch = (UBYTE *)AllocMem(need, MEMF_PUBLIC);
            if (!g_jpeg_scratch) {
                mp_log_text("JPEG scratch allocation failed");
                mp_job_cleanup();
                return FALSE;
            }
            break;
    }

    g_job_open = mp_spool_job_open(mp_job_filename());
    if (!g_job_open) {
        mp_log_text("Job open failed for output file");
        mp_job_cleanup();
        return FALSE;
    }

    switch (g_engine) {
        case MP_ENGINE_PWG:
            if (!mp_pwg_begin(&g_pwg, width, height, g_pwg_scratch,
                              g_pwg_scratch_bytes, mp_job_file_write, NULL)) {
                mp_log_text("PWG encoder begin failed");
                g_job_failed = TRUE;
                mp_job_cleanup();
                return FALSE;
            }
            mp_log_3("PWG begin width/height/scratch",
                     (LONG)width, (LONG)height, (LONG)g_pwg_scratch_bytes);
            break;
        case MP_ENGINE_PDF:
            if (!mp_pdf_begin(&g_pdf, width, height, g_pdf_scratch,
                              g_pdf_scratch_bytes, mp_job_file_write, NULL)) {
                mp_log_text("PDF encoder begin failed");
                g_job_failed = TRUE;
                mp_job_cleanup();
                return FALSE;
            }
            mp_log_3("PDF begin width/height/scratch",
                     (LONG)width, (LONG)height, (LONG)g_pdf_scratch_bytes);
            break;
        default:
            if (!mp_jpeg_begin(&g_jpeg, width, height, g_jpeg_scratch,
                               g_jpeg_scratch_bytes, mp_job_file_write, NULL)) {
                mp_log_text("JPEG encoder begin failed");
                g_job_failed = TRUE;
                mp_job_cleanup();
                return FALSE;
            }
            mp_log_3("JPEG begin width/height/scratch",
                     (LONG)width, (LONG)height, (LONG)g_jpeg_scratch_bytes);
            break;
    }

    return TRUE;
}

static BOOL mp_job_write_row(struct PrtInfo *pi, ULONG row_number)
{
    ULONG src_x;
    ULONG dst_x;
    ULONG i;

    if (!g_job_open || !g_rgb_row || !pi || !pi->pi_ColorInt) return FALSE;

    for (i = 0; i < g_rgb_row_bytes; ++i) g_rgb_row[i] = 255;
    dst_x = (ULONG)pi->pi_xpos;

    for (src_x = 0; src_x < (ULONG)pi->pi_width && dst_x < g_page_width; ++src_x) {
        union colorEntry *pixel = &pi->pi_ColorInt[src_x];
        ULONG repeat = pi->pi_ScaleX ? (ULONG)pi->pi_ScaleX[src_x] : 1UL;
        UBYTE red   = (UBYTE)(255U - pixel->colorByte[PCMCYAN]);
        UBYTE green = (UBYTE)(255U - pixel->colorByte[PCMMAGENTA]);
        UBYTE blue  = (UBYTE)(255U - pixel->colorByte[PCMYELLOW]);

        while (repeat-- && dst_x < g_page_width) {
            ULONG out = dst_x * 3UL;
            g_rgb_row[out + 0] = red;
            g_rgb_row[out + 1] = green;
            g_rgb_row[out + 2] = blue;
            ++dst_x;
        }
    }

    if (row_number != g_job_rows_written) {
        mp_log_3("Non-sequential row got/expected/height",
                 (LONG)row_number, (LONG)g_job_rows_written,
                 (LONG)g_page_height);
    }

    switch (g_engine) {
        case MP_ENGINE_PWG:
            if (!mp_pwg_write_scanline(&g_pwg, g_rgb_row)) {
                mp_log_3("PWG scanline failed row/written/expected",
                         (LONG)row_number, (LONG)g_job_rows_written,
                         (LONG)g_page_height);
                g_job_failed = TRUE;
                return FALSE;
            }
            break;
        case MP_ENGINE_PDF:
            if (!mp_pdf_write_scanline(&g_pdf, g_rgb_row)) {
                mp_log_3("PDF scanline failed row/written/expected",
                         (LONG)row_number, (LONG)g_job_rows_written,
                         (LONG)g_page_height);
                g_job_failed = TRUE;
                return FALSE;
            }
            break;
        default:
            if (!mp_jpeg_write_scanline(&g_jpeg, g_rgb_row)) {
                mp_log_3("JPEG scanline failed row/written/expected",
                         (LONG)row_number, (LONG)g_job_rows_written,
                         (LONG)g_page_height);
                g_job_failed = TRUE;
                return FALSE;
            }
            break;
    }

    ++g_job_rows_written;
    return TRUE;
}

static BOOL mp_job_finish(void)
{
    BOOL ok = FALSE;
    const char *label;

    if (!g_job_failed && g_job_open && g_job_rows_written == g_page_height) {
        switch (g_engine) {
            case MP_ENGINE_PWG: ok = mp_pwg_finish(&g_pwg) ? TRUE : FALSE; break;
            case MP_ENGINE_PDF: ok = mp_pdf_finish(&g_pdf) ? TRUE : FALSE; break;
            default:            ok = mp_jpeg_finish(&g_jpeg) ? TRUE : FALSE; break;
        }
        if (!ok) g_job_failed = TRUE;
    } else {
        g_job_failed = TRUE;
    }

    switch (g_engine) {
        case MP_ENGINE_PWG: label = "PWG end rows/expected/failed"; break;
        case MP_ENGINE_PDF: label = "PDF end rows/expected/failed"; break;
        default:            label = "JPEG end rows/expected/failed"; break;
    }
    mp_log_3(label, (LONG)g_job_rows_written, (LONG)g_page_height,
             g_job_failed ? 1 : 0);
    mp_job_cleanup();
    return ok;
}

static void mp_log_row(struct PrtInfo *pi, ULONG row)
{
    ULONG i;
    ULONG scaled_width = 0;

    if (!pi) return;

    if (pi->pi_ScaleX) {
        for (i = 0; i < pi->pi_width; ++i)
            scaled_width += pi->pi_ScaleX[i];
    } else {
        scaled_width = pi->pi_width;
    }

    mp_log_reset();
    mp_log_append("MintPRINT: row=");
    mp_log_append_long((LONG)row);
    mp_log_append(" sourceWidth=");
    mp_log_append_long((LONG)pi->pi_width);
    mp_log_append(" scaledWidth=");
    mp_log_append_long((LONG)scaled_width);

    if (pi->pi_ColorInt && pi->pi_width) {
        union colorEntry *p = &pi->pi_ColorInt[0];
        mp_log_append(" firstYMCB=");
        mp_log_append_long(p->colorByte[PCMYELLOW]);
        mp_log_append(",");
        mp_log_append_long(p->colorByte[PCMMAGENTA]);
        mp_log_append(",");
        mp_log_append_long(p->colorByte[PCMCYAN]);
        mp_log_append(",");
        mp_log_append_long(p->colorByte[PCMBLACK]);
    }

    mp_spool_log(g_log_line);
}

LONG PRT_STDARGS Init(struct PrinterData *pd)
{
    /* SysBase has to be valid before calling any exec.library function. */
    SysBase = *(struct ExecBase **)4L;
    PD = pd;
    PED = &PEDData;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (!DOSBase) {
        return -1;
    }

    mp_config_defaults(&g_config);
    mp_spool_ensure_running();
    mp_log_text("Init");
    return 0;
}

VOID PRT_STDARGS Expunge(void)
{
    mp_log_text("Expunge");
    mp_job_cleanup();
    mp_spool_shutdown();

    if (DOSBase) {
        CloseLibrary((struct Library *)DOSBase);
        DOSBase = NULL;
    }

    PD = NULL;
    PED = NULL;
}

int PRT_STDARGS DriverOpen(struct IORequest *ior)
{
    (void)ior;
    mp_log_text("Open");
    return 0;
}

VOID PRT_STDARGS DriverClose(struct IORequest *ior)
{
    (void)ior;
    mp_job_cleanup();
    mp_log_text("Close");
}

LONG PRT_STDARGS DoSpecial(UWORD *command, UBYTE output_buffer[],
                           BYTE *current_line_position,
                           BYTE *current_line_spacing,
                           BYTE *crlf_flag, STRPTR params)
{
    (void)command;
    (void)output_buffer;
    (void)current_line_position;
    (void)current_line_spacing;
    (void)crlf_flag;
    (void)params;

    /* No text-printer escape language yet. Graphics is the first milestone. */
    return 0;
}

LONG PRT_STDARGS Render(LONG ct, LONG x, LONG y, LONG status, ...)
{
    switch (status) {
        case 5: /* Pre-master initialisation. x = io_Special density flags. */
            if (PED) {
                PED->ped_MaxXDots = 4096;
                PED->ped_MaxYDots = 6144;
                PED->ped_XDotsInch = 300;
                PED->ped_YDotsInch = 300;
                PED->ped_NumRows = 1;
            }
            mp_log_3("Render pre-master special/maxX/maxY", x, 4096, 6144);
            return PDERR_NOERR;

        case 0: /* Master initialisation: x/y are final output dimensions. */
            g_page_width = (ULONG)x;
            g_page_height = (ULONG)y;
            g_rows_seen = 0;
            g_config_source = mp_spool_config_load(&g_config);
            mp_log_3("Render begin width/height/ct", x, y, ct ? 1 : 0);
            mp_log_config(&g_config, g_config_source);

            if (!mp_job_begin(g_page_width, g_page_height))
                return PDERR_BUFFERMEMORY;

            return PDERR_NOERR;

        case 1: { /* Raster row: ct -> PrtInfo, y = output row number. */
            struct PrtInfo *pi = (struct PrtInfo *)ct;
            ++g_rows_seen;

            if ((ULONG)y == 0 ||
                (g_page_height && (ULONG)y == (g_page_height / 2)) ||
                (g_page_height && ((ULONG)y + 1) == g_page_height)) {
                mp_log_row(pi, (ULONG)y);
            }

            if (!g_job_failed && !mp_job_write_row(pi, (ULONG)y))
                return PDERR_CANCEL;

            return PDERR_NOERR;
        }

        case 2: /* Printer buffer would normally be sent here. */
            return PDERR_NOERR;

        case 3: /* Printer buffer would normally be cleared here. */
            return PDERR_NOERR;

        case 4: /* Close down. ct = final error code. */
            mp_log_3("Render end status/rows/expected", ct,
                     (LONG)g_rows_seen, (LONG)g_page_height);
            {
                BOOL job_ok = mp_job_finish();
                if (ct == 0 && job_ok) {
                    struct MPIPPResult result;
                    LONG ipp_rc;
                    CONST_STRPTR fname = mp_job_filename();
                    CONST_STRPTR fmt;
                    switch (g_engine) {
                        case MP_ENGINE_PWG: fmt = (CONST_STRPTR)"image/pwg-raster"; break;
                        case MP_ENGINE_PDF: fmt = (CONST_STRPTR)"application/pdf"; break;
                        default:            fmt = (CONST_STRPTR)"image/jpeg"; break;
                    }
                    ipp_rc = mp_spool_ipp_submit(&g_config, fname, fmt, &result);
                    mp_log_3("IPP result error/http/status",
                             result.error, result.http_status,
                             (LONG)result.ipp_status);
                    if (ipp_rc != 0) return PDERR_CANCEL;
                    if (!g_config.keep_job)
                        mp_spool_job_delete(fname);
                }
            }
            return PDERR_NOERR;

        case 6: /* Multi-pass colour change; not used by PCC_YMCB. */
            return PDERR_NOERR;

        default:
            mp_log_3("Render unknown status/x/y", status, x, y);
            return PDERR_NOERR;
    }
}
