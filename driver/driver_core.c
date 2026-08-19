/*
 * MintPRINT printer.device integration working driver path.
 *
 * Converts printer.device raster rows into a low-memory streaming document
 * (JPEG, or PWG Raster per Unit0's ENGINE= setting) and submits it to the
 * configured IPP Print-Job endpoint.
 *
 * Trace output: T:MintPRINT-driver.log
 * Debug JPEG:      T:MintPRINT-job.jpg
 * Debug PWG Raster: T:MintPRINT-job.pwg
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
#include "ipp_client.h"

struct ExecBase *SysBase = NULL;
struct DosLibrary *DOSBase = NULL;
struct PrinterData *PD = NULL;
struct PrinterExtendedData *PED = NULL;

extern struct PrinterExtendedData PEDData;

/*
 * OS 3.5+/3.2 extended printer-driver features:
 * - PRTA_NoIO: printer.device must not open parallel/serial transport.
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

static ULONG g_page_width = 0;
static ULONG g_page_height = 0;
static ULONG g_rows_seen = 0;

static BPTR g_job_fh = 0;
static UBYTE *g_rgb_row = NULL;
static ULONG g_rgb_row_bytes = 0;
static UBYTE *g_jpeg_scratch = NULL;
static ULONG g_jpeg_scratch_bytes = 0;
static UBYTE *g_pwg_scratch = NULL;
static ULONG g_pwg_scratch_bytes = 0;
static ULONG g_job_rows_written = 0;
static BOOL g_job_failed = FALSE;
static BOOL g_using_pwg = FALSE;
static MPJpegEncoder g_jpeg;
static MPPwgEncoder g_pwg;
static struct MPConfig g_config;
static LONG g_config_source = MP_CONFIG_SOURCE_DEFAULTS;

static CONST_STRPTR mp_job_filename(void)
{
    return g_using_pwg ? MP_JOB_FILE_PWG : MP_JOB_FILE_JPEG;
}

static ULONG mp_strlen(const char *s)
{
    ULONG n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void mp_write_text(BPTR fh, const char *s)
{
    ULONG len = mp_strlen(s);
    if (fh && len) Write(fh, (APTR)s, len);
}

static void mp_write_long(BPTR fh, LONG value)
{
    char buf[16];
    ULONG pos = sizeof(buf);
    ULONG v;

    if (!fh) return;

    if (value < 0) {
        mp_write_text(fh, "-");
        v = (ULONG)(-value);
    } else {
        v = (ULONG)value;
    }

    if (v == 0) {
        mp_write_text(fh, "0");
        return;
    }

    while (v && pos) {
        buf[--pos] = (char)('0' + (v % 10));
        v /= 10;
    }
    Write(fh, &buf[pos], sizeof(buf) - pos);
}

static BPTR mp_open_log(void)
{
    BPTR fh;

    if (!DOSBase) return 0;

    fh = Open((CONST_STRPTR)"T:MintPRINT-driver.log", MODE_READWRITE);
    if (!fh) fh = Open((CONST_STRPTR)"T:MintPRINT-driver.log", MODE_NEWFILE);
    if (fh) Seek(fh, 0, OFFSET_END);
    return fh;
}

static void mp_log_text(const char *event)
{
    BPTR fh = mp_open_log();
    if (!fh) return;

    mp_write_text(fh, "MintPRINT: ");
    mp_write_text(fh, event);
    mp_write_text(fh, "\n");
    Close(fh);
}

static void mp_log_3(const char *event, LONG a, LONG b, LONG c)
{
    BPTR fh = mp_open_log();
    if (!fh) return;

    mp_write_text(fh, "MintPRINT: ");
    mp_write_text(fh, event);
    mp_write_text(fh, " ");
    mp_write_long(fh, a);
    mp_write_text(fh, " ");
    mp_write_long(fh, b);
    mp_write_text(fh, " ");
    mp_write_long(fh, c);
    mp_write_text(fh, "\n");
    Close(fh);
}

static void mp_log_config(const struct MPConfig *cfg, LONG source)
{
    BPTR fh = mp_open_log();
    if (!fh || !cfg) return;

    mp_write_text(fh, "MintPRINT: Config source=");
    if (source == MP_CONFIG_SOURCE_ENV)
        mp_write_text(fh, "ENV");
    else if (source == MP_CONFIG_SOURCE_ENVARC)
        mp_write_text(fh, "ENVARC");
    else
        mp_write_text(fh, "defaults");
    mp_write_text(fh, " host=");
    mp_write_text(fh, cfg->host);
    mp_write_text(fh, " port=");
    mp_write_long(fh, (LONG)cfg->port);
    mp_write_text(fh, " path=");
    mp_write_text(fh, cfg->path);
    mp_write_text(fh, " keepjob=");
    mp_write_long(fh, cfg->keep_job ? 1 : 0);
    if (cfg->media[0]) { mp_write_text(fh, " media="); mp_write_text(fh, cfg->media); }
    if (cfg->source[0]) { mp_write_text(fh, " source="); mp_write_text(fh, cfg->source); }
    if (cfg->color[0]) { mp_write_text(fh, " color="); mp_write_text(fh, cfg->color); }
    if (cfg->quality[0]) { mp_write_text(fh, " quality="); mp_write_text(fh, cfg->quality); }
    if (cfg->scaling[0]) { mp_write_text(fh, " scaling="); mp_write_text(fh, cfg->scaling); }
    if (cfg->sides[0]) { mp_write_text(fh, " sides="); mp_write_text(fh, cfg->sides); }
    mp_write_text(fh, " engine="); mp_write_text(fh, cfg->engine[0] ? cfg->engine : "jpeg");
    mp_write_text(fh, "\n");
    Close(fh);
}

static BOOL mp_write_all(BPTR fh, const UBYTE *data, ULONG length)
{
    ULONG done = 0;

    if (!fh || (!data && length)) return FALSE;

    while (done < length) {
        LONG n = Write(fh, (APTR)(data + done), (LONG)(length - done));
        if (n <= 0) return FALSE;
        done += (ULONG)n;
    }

    return TRUE;
}

static long mp_job_file_write(void *ctx, const unsigned char *data, unsigned long length)
{
    (void)ctx;
    return mp_write_all(g_job_fh, (const UBYTE *)data, (ULONG)length) ? (long)length : -1;
}

static void mp_job_cleanup(void)
{
    if (g_job_fh) {
        Close(g_job_fh);
        g_job_fh = 0;
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
    g_rgb_row_bytes = 0;
    g_jpeg_scratch_bytes = 0;
    g_pwg_scratch_bytes = 0;
}

static BOOL mp_job_begin(ULONG width, ULONG height)
{
    ULONG need;

    mp_job_cleanup();
    g_job_rows_written = 0;
    g_job_failed = FALSE;
    g_using_pwg = (g_config.engine[0] == 'p');

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

    need = g_using_pwg ? mp_pwg_scratch_size(width) : mp_jpeg_scratch_size(width);
    if (!need) {
        mp_log_text("Encoder scratch size rejected width");
        mp_job_cleanup();
        return FALSE;
    }

    if (g_using_pwg) {
        g_pwg_scratch_bytes = need;
        g_pwg_scratch = (UBYTE *)AllocMem(need, MEMF_PUBLIC);
        if (!g_pwg_scratch) {
            mp_log_text("PWG scratch allocation failed");
            mp_job_cleanup();
            return FALSE;
        }
    } else {
        g_jpeg_scratch_bytes = need;
        g_jpeg_scratch = (UBYTE *)AllocMem(need, MEMF_PUBLIC);
        if (!g_jpeg_scratch) {
            mp_log_text("JPEG scratch allocation failed");
            mp_job_cleanup();
            return FALSE;
        }
    }

    g_job_fh = Open(mp_job_filename(), MODE_NEWFILE);
    if (!g_job_fh) {
        mp_log_text("Job open failed for output file");
        mp_job_cleanup();
        return FALSE;
    }

    if (g_using_pwg) {
        if (!mp_pwg_begin(&g_pwg, width, height, g_pwg_scratch,
                          g_pwg_scratch_bytes, mp_job_file_write, NULL)) {
            mp_log_text("PWG encoder begin failed");
            g_job_failed = TRUE;
            mp_job_cleanup();
            return FALSE;
        }
        mp_log_3("PWG begin width/height/scratch",
                 (LONG)width, (LONG)height, (LONG)g_pwg_scratch_bytes);
    } else {
        if (!mp_jpeg_begin(&g_jpeg, width, height, g_jpeg_scratch,
                           g_jpeg_scratch_bytes, mp_job_file_write, NULL)) {
            mp_log_text("JPEG encoder begin failed");
            g_job_failed = TRUE;
            mp_job_cleanup();
            return FALSE;
        }
        mp_log_3("JPEG begin width/height/scratch",
                 (LONG)width, (LONG)height, (LONG)g_jpeg_scratch_bytes);
    }

    return TRUE;
}

static BOOL mp_job_write_row(struct PrtInfo *pi, ULONG row_number)
{
    ULONG src_x;
    ULONG dst_x;
    ULONG i;

    if (!g_job_fh || !g_rgb_row || !pi || !pi->pi_ColorInt) return FALSE;

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

    if (g_using_pwg) {
        if (!mp_pwg_write_scanline(&g_pwg, g_rgb_row)) {
            mp_log_3("PWG scanline failed row/written/expected",
                     (LONG)row_number, (LONG)g_job_rows_written,
                     (LONG)g_page_height);
            g_job_failed = TRUE;
            return FALSE;
        }
    } else {
        if (!mp_jpeg_write_scanline(&g_jpeg, g_rgb_row)) {
            mp_log_3("JPEG scanline failed row/written/expected",
                     (LONG)row_number, (LONG)g_job_rows_written,
                     (LONG)g_page_height);
            g_job_failed = TRUE;
            return FALSE;
        }
    }

    ++g_job_rows_written;
    return TRUE;
}

static BOOL mp_job_finish(void)
{
    BOOL ok = FALSE;

    if (!g_job_failed && g_job_fh && g_job_rows_written == g_page_height) {
        ok = (g_using_pwg ? mp_pwg_finish(&g_pwg) : mp_jpeg_finish(&g_jpeg)) ? TRUE : FALSE;
        if (!ok) g_job_failed = TRUE;
    } else {
        g_job_failed = TRUE;
    }

    mp_log_3(g_using_pwg ? "PWG end rows/expected/failed" : "JPEG end rows/expected/failed",
             (LONG)g_job_rows_written, (LONG)g_page_height,
             g_job_failed ? 1 : 0);
    mp_job_cleanup();
    return ok;
}

static void mp_log_row(struct PrtInfo *pi, ULONG row)
{
    BPTR fh;
    ULONG i;
    ULONG scaled_width = 0;

    if (!pi) return;

    if (pi->pi_ScaleX) {
        for (i = 0; i < pi->pi_width; ++i)
            scaled_width += pi->pi_ScaleX[i];
    } else {
        scaled_width = pi->pi_width;
    }

    fh = mp_open_log();
    if (!fh) return;

    mp_write_text(fh, "MintPRINT: row=");
    mp_write_long(fh, (LONG)row);
    mp_write_text(fh, " sourceWidth=");
    mp_write_long(fh, (LONG)pi->pi_width);
    mp_write_text(fh, " scaledWidth=");
    mp_write_long(fh, (LONG)scaled_width);

    if (pi->pi_ColorInt && pi->pi_width) {
        union colorEntry *p = &pi->pi_ColorInt[0];
        mp_write_text(fh, " firstYMCB=");
        mp_write_long(fh, p->colorByte[PCMYELLOW]);
        mp_write_text(fh, ",");
        mp_write_long(fh, p->colorByte[PCMMAGENTA]);
        mp_write_text(fh, ",");
        mp_write_long(fh, p->colorByte[PCMCYAN]);
        mp_write_text(fh, ",");
        mp_write_long(fh, p->colorByte[PCMBLACK]);
    }

    mp_write_text(fh, "\n");
    Close(fh);
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
    mp_log_text("Init");
    return 0;
}

VOID PRT_STDARGS Expunge(void)
{
    mp_log_text("Expunge");
    mp_job_cleanup();

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
            g_config_source = mp_config_load(&g_config);
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
                    CONST_STRPTR fmt = g_using_pwg ? (CONST_STRPTR)"image/pwg-raster"
                                                    : (CONST_STRPTR)"image/jpeg";
                    ipp_rc = mp_ipp_print_document(&g_config, fname, fmt, &result);
                    mp_log_3("IPP result error/http/status",
                             result.error, result.http_status,
                             (LONG)result.ipp_status);
                    if (ipp_rc != 0) return PDERR_CANCEL;
                    if (!g_config.keep_job)
                        DeleteFile(fname);
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
