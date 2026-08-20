/* MintPrint Settings (formerly IPP-Test16.c / "MintPRINT Preferences").
   Setup/test GUI for the DEVS:Printers/MintPRINT driver: LAN printer
   discovery, IPP capability query, driver install/select helper, and
   per-job defaults editing. */
/* MintPRINT GUI stabilised: no live cycle-label frees; safe teardown. */
/* MintPRINT prefs #9: compact address row and status-box fit. */
/* MintPRINT prefs #8: capability cache and output-area layout polish. */
/* Amiga IPP Print-Job Prototype with GUI
   Sends a JPEG file to an IPP printer (AirPrint-compatible)
   Compile with: m68k-amigaos-gcc -g -o IPP-test11 ipp-test11.c -lamiga -lsocket -lm
 PATCH INCOMING: Adds IFF -> RGB -> PWG -> IPP printing support to IPP-test15 */


#include <proto/exec.h>
#include <ctype.h> // for tolower()
#include <proto/dos.h>
#include <dos/dostags.h> // for SYS_Asynch (SystemTags)
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
typedef long ssize_t;
#include <datatypes/pictureclass.h>
#include <datatypes/datatypesclass.h>
#include <clib/alib_protos.h>
#include <proto/bsdsocket.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <devices/printer.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for O_NONBLOCK
#include "iff-loader.h"

/* All status/progress output goes to the on-screen status box, never a
 * console - the end user may have launched this from Workbench, where
 * there is no console to see it in. custom_printf() itself is defined
 * further down (it draws into the on-screen output box); forward-declare
 * it and redirect printf() to it here, before any of this file's own
 * printf() calls, so every one of them lands in the box consistently. */
void custom_printf(const char *format, ...);
#define printf custom_printf

extern struct GfxBase *GfxBase;
#define MAX_VALUES 32
#define MAX_ATTR_LEN 64
#define MAX_BUFFER 256000
#define MAX_OUTPUT_LINES 10
#define MAX_OUTPUT_LINE_LENGTH 47
#define MAX_PRINT_MODES 8
#define MAX_QUALITIES 5
#define MENU_ID_FILE       1
#define MENU_ID_FILE_SAVE  2
#define MENU_ID_FILE_QUIT  3

// Gadget IDs
#define GAD_IP_STRING 1
#define GAD_FILE_STRING 2
#define GAD_QUERY_BUTTON 3
#define GAD_PRINT_BUTTON 4
#define GAD_EXIT_BUTTON 5
#define GAD_MEDIA_DROPDOWN 6
#define GAD_PRINT_MODE 7
#define GAD_SCALING_MODE 8
#define GAD_QUALITY_MODE 9
#define GAD_IPP_PATH 10
#define GAD_KEEPJOB 11
#define GAD_ENGINE 12
#define GAD_SAVE_BUTTON 13
#define GAD_DISCOVER_BUTTON 14
#define GAD_UNIT_DROPDOWN 15
#define GAD_SET_ACTIVE_BUTTON 16
#define GAD_MODEL_DISPLAY 17

// Discovery selection dialog gadget IDs (separate window/gadget list)
#define GAD_DISC_CYCLE  1
#define GAD_DISC_USE    2
#define GAD_DISC_CANCEL 3

#define MAX_DISCOVERY_RESULTS 16

struct DiscoveredPrinter {
    char ip[16];
    char label[80];
};

// Saved printer profiles: ENV:MintPRINT/Unit0 .. Unit(MAX_UNITS-1). Only
// Unit0 is what the driver actually reads at print time; the others are
// switchable GUI-side profiles (e.g. for a second/third network printer).
#define MAX_UNITS 8

#define OUTPUT_TOP     265 // Below Test Print / Save / Exit row
#define OUTPUT_LEFT    10
#define OUTPUT_LINE_H  8
#define OUTPUT_LINES   MAX_OUTPUT_LINES
#define OUTPUT_BOTTOM  (OUTPUT_TOP + (OUTPUT_LINE_H * OUTPUT_LINES) - 1)
#define OUTPUT_RIGHT   (window->Width - 20)

// Define the USED macro for GCC
#define USED __attribute__((used))
// Simple extension check
BOOL has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return FALSE;
    return strcasecmp(dot, ext) == 0;
}
/*
 * Classic AmigaOS / libnix stack request.
 *
 * The "$STACK:" cookie is useful on newer AmigaOS startup code, but classic
 * m68k AmigaOS programs built with the GCC/libnix runtime use the __stack
 * variable.  Keep this comfortably large because the Settings Query path
 * has deep parsing / GadTools / bsdsocket call chains.
 *
 * 384 KiB = 393216 bytes.
 */
unsigned long __stack = 393216UL;

/* Keep the cookie as harmless metadata for newer startup code too. */
static const char USED min_stack[] = "$STACK:393216";

// Structure to map media sizes to trays (Updated to include tray name and medianame)
struct MediaTrayMap {
    char media[MAX_ATTR_LEN];      // e.g., "iso_a4_210x297mm"
    char source[MAX_ATTR_LEN];     // e.g., "by-pass-tray"
    char trayName[MAX_ATTR_LEN];   // e.g., "MP TRAY"
    char medianame[MAX_ATTR_LEN];  // e.g., "INKJET"
};

// Globals for parsed capabilities
char supported_formats[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_formats = 0;

char supported_media[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_media = 0;

char supported_output_modes[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_output_modes = 0;

char supported_sides[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_sides = 0;

char supported_scaling[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_scaling = 0;

int supported_orientations[MAX_VALUES];
int num_supported_orientations = 0;

char supported_print_modes[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_print_modes = 0;

char *print_mode_options[MAX_PRINT_MODES];
int num_print_modes = 0;
static char initial_print_mode_value[MAX_ATTR_LEN] = "Not Detected";
static STRPTR initial_print_mode[] = { initial_print_mode_value, NULL };
char selected_print_mode[MAX_ATTR_LEN] = "monochrome"; // Default fallback
char selected_scaling[MAX_ATTR_LEN] = "auto"; // Default
static char initial_scaling_value[MAX_ATTR_LEN] = "Not Detected";
static STRPTR initial_scaling_mode[] = { initial_scaling_value, NULL };
/* MintPRINT stable Cycle gadget label storage (OS3.1-safe).
 *
 * Classic GadTools is much happier when GTCY_Labels keeps the same array
 * address for the complete lifetime of a live CYCLE_KIND gadget. Earlier
 * versions repeatedly AllocVec'd new arrays during Query/Save and retargeted
 * the live gadgets. That could leave V37 GadTools with stale bookkeeping,
 * producing delayed memory alerts or a hard lock during a later Query/Exit.
 *
 * Keep both the pointer arrays and their text backing in static storage.
 * Query rewrites the contents in-place and re-applies THE SAME pointer.
 */
#define MP_MEDIA_LABEL_LEN   (MAX_ATTR_LEN + 32)
#define MP_UNIT_LABEL_LEN    128

static char mp_media_label_storage[MAX_VALUES + 1][MP_MEDIA_LABEL_LEN];
static STRPTR mp_media_label_ptrs[MAX_VALUES + 2];

static char mp_scaling_label_storage[MAX_VALUES + 1][MAX_ATTR_LEN];
static STRPTR mp_scaling_label_ptrs[MAX_VALUES + 2];

static char mp_print_mode_label_storage[MAX_VALUES + 1][MAX_ATTR_LEN];
static STRPTR mp_print_mode_label_ptrs[MAX_VALUES + 2];

static char mp_quality_label_storage[MAX_VALUES + 1][32];
static STRPTR mp_quality_label_ptrs[MAX_VALUES + 2];

static char mp_unit_label_storage[MAX_UNITS][MP_UNIT_LABEL_LEN];
static STRPTR mp_unit_label_ptrs[MAX_UNITS + 1];

STRPTR *scaling_mode_labels = mp_scaling_label_ptrs;
char selected_quality[16] = "auto"; // Default
char supported_quality[MAX_QUALITIES][16];
int num_supported_quality = 0;
STRPTR *quality_mode_labels = mp_quality_label_ptrs;
static char initial_quality_value[32] = "Not Detected";
static STRPTR initial_quality_mode[] = { initial_quality_value, NULL };
// Media dropdown state
char *selected_media = NULL;
struct Gadget *media_dropdown = NULL;
STRPTR *media_dropdown_items = mp_media_label_ptrs;
BOOL has_media_ready = FALSE;
struct Menu *menu = NULL;
struct MediaTrayMap media_tray_map[MAX_VALUES];
int num_media_tray_mappings = 0;

// Radio button labels for print mode
STRPTR *print_mode_labels = mp_print_mode_label_ptrs;

static char initial_media_value[160] = "Not Detected";
static STRPTR initial_media_labels[] = { initial_media_value, NULL };



static struct NewMenu menu_template[] = {
    { NM_TITLE, (STRPTR)"File", 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Save Driver Settings", 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Reload Driver Settings", 0, 0, 0, 0 },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"About MintPRINT...", 0, 0, 0, 0 },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Quit", 0, 0, 0, 0 },
    { NM_END,   NULL, 0, 0, 0, 0 }
};
// Global variable to store the print mode (0 = Black and White, 1 = Color)
int print_mode = 0; // Default to Black and White

// Helper to store values into lists
void store_value(char dest[MAX_VALUES][MAX_ATTR_LEN], int *count, const char *value) {
    if (*count >= MAX_VALUES) return;
    strncpy(dest[*count], value, MAX_ATTR_LEN - 1);
    dest[*count][MAX_ATTR_LEN - 1] = '\0';
    (*count)++;
}

void store_int_value(int dest[MAX_VALUES], int *count, int val) {
    if (*count >= MAX_VALUES) return;
    dest[(*count)++] = val;
}

// Media Size Helper
BOOL parse_media_dimensions(const char *media_str, int *x, int *y) {
    const char *dim_part = strchr(media_str, '_');
    if (!dim_part) return FALSE;

    int w, h;
    if (sscanf(dim_part + 1, "%dx%dmm", &w, &h) == 2) {
        *x = w * 100; // Convert mm to hundredths of mm
        *y = h * 100;
        return TRUE;
    }
    return FALSE;
}

void ensure_quality_defaults() {
    if (num_supported_quality == 0) {
        printf("No print-quality-supported returned by printer, falling back to default values.\n");

        strcpy(supported_quality[0], "draft");
        strcpy(supported_quality[1], "normal");
        strcpy(supported_quality[2], "high");
        num_supported_quality = 3;
    }
}

//Helper to parse IP and port from GUI
int parse_ip_and_port(const char *input, char *ip_out, int ip_len, int *port_out) {
    char *colon = strchr(input, ':');
    if (colon) {
        int len = colon - input;
        if (len >= ip_len) return 0;
        strncpy(ip_out, input, len);
        ip_out[len] = '\0';
        *port_out = atoi(colon + 1);
    } else {
        strncpy(ip_out, input, ip_len - 1);
        ip_out[ip_len - 1] = '\0';
        *port_out = -1;  // no port specified
    }
    return 1;
}


//Safe Send Data
int safe_send(int sockfd, const void *vbuf, int len) {
    const char *buf = (const char *)vbuf;
    int total_sent = 0;
    int attempt = 0;

    while (total_sent < len) {
        int chunk_size = (len - total_sent > 4096) ? 4096 : (len - total_sent);
        int sent = send(sockfd, (char *)buf + total_sent, chunk_size, 0);
        attempt++;

        if (sent <= 0) {
            printf("\n[!] send() failed at %d bytes (attempt %d)\n", total_sent, attempt);
            perror("send");
            return -1;
        }

        total_sent += sent;

        // Progress bar (every 64KB or on finish)
        if ((total_sent % 65536 == 0) || (total_sent == len)) {
            printf("[+] Sent %d / %d bytes (%d%%)\n", total_sent, len, (total_sent * 100) / len);
        }
    }

    printf("[✓] Finished sending %d bytes successfully\n", total_sent);
    return total_sent;
}

// Locates the byte offset just past the next "\r\n\r\n" header terminator at
// or after `start`, or -1 if the buffer doesn't contain one (yet). Some IPP
// printers (observed: a Canon TS8300) send an interim
// "HTTP/1.1 100 Continue\r\n\r\n" ahead of the real response; callers
// re-invoke this with `start` advanced past that block (see mp_http_status)
// to skip it rather than mistake it for the real header/body boundary.
static int mp_http_find_body(const char *buf, int len, int start) {
    int i;
    if (len < 4 || start < 0 || start + 4 > len) return -1;
    for (i = start; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    return -1;
}

// Parses the 3-digit status code from the "HTTP/1.x NNN ..." status line
// starting at `start`. Returns 0 if it can't be parsed.
static int mp_http_status(const char *buf, int len, int start) {
    int i = start;
    while (i < len && buf[i] != ' ') i++;
    if (i + 4 > len) return 0;
    if (buf[i + 1] < '0' || buf[i + 1] > '9' || buf[i + 2] < '0' || buf[i + 2] > '9' ||
        buf[i + 3] < '0' || buf[i + 3] > '9') return 0;
    return (buf[i + 1] - '0') * 100 + (buf[i + 2] - '0') * 10 + (buf[i + 3] - '0');
}

// Global variables for GUI
struct Window *window = NULL;
struct Gadget *glist = NULL;
struct Library *SocketBase = NULL;
struct Library *GadToolsBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
char ip_buffer[256] = "192.168.0.51:80";
char driver_path_buffer[96] = "/ipp/print";
BOOL driver_keep_job = TRUE;
static STRPTR keep_job_labels[] = { "Delete job JPEG", "Keep debug JPEG", NULL };
char driver_engine_buffer[32] = "jpeg";
#define MP_ENGINE_MAX 3

static const char *mp_engine_all_labels[MP_ENGINE_MAX] = {
    "JPEG", "PWG Raster", "PDF"
};
static const char *mp_engine_all_values[MP_ENGINE_MAX] = {
    "jpeg", "pwg-raster", "pdf"
};
static const char *mp_engine_all_mimes[MP_ENGINE_MAX] = {
    "image/jpeg", "image/pwg-raster", "application/pdf"
};

/* This array address stays fixed for the lifetime of the GadTools Cycle. */
static STRPTR engine_labels[MP_ENGINE_MAX + 1] = {
    "JPEG", "PWG Raster", "PDF", NULL
};

/* Maps the currently-visible Cycle index to MintPRINT's internal value. */
static const char *mp_engine_value_map[MP_ENGINE_MAX] = {
    "jpeg", "pwg-raster", "pdf"
};
static int mp_engine_count = MP_ENGINE_MAX;
char driver_media_buffer[MAX_ATTR_LEN] = "";
char driver_source_buffer[MAX_ATTR_LEN] = "";
char driver_color_buffer[MAX_ATTR_LEN] = "";
char driver_quality_buffer[MAX_ATTR_LEN] = "";
char driver_scaling_buffer[MAX_ATTR_LEN] = "";
char driver_sides_buffer[MAX_ATTR_LEN] = "";
int current_unit_index = 0;
char printer_make_model[128] = "";
STRPTR *unit_dropdown_labels = mp_unit_label_ptrs;
/* MintPRINT prefs #6: queried job defaults are saved into Unit0. */
/* MintPRINT prefs #7: saved-state placeholders, ghosting, layout and engine selector. */
char file_buffer[256] = "UHD:test.jpg";
char output_buffer[MAX_OUTPUT_LINES][MAX_OUTPUT_LINE_LENGTH];
char supported_media_sources[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_media_sources = 0;
int output_line = 0;
struct Screen *screen = NULL;
void *vi = NULL;
struct TextFont *font = NULL;
BOOL operation_in_progress = FALSE;

// Font definition
struct TextAttr Topaz80 = {
    "topaz.font",
    8,
    0,
    0
};

struct TextAttr Topaz60 = {
    "topaz.font",
    6,
    0,
    0
};

// Save print mode to ENV:
void save_print_mode(void) {
    BPTR file = Open("ENV:IPP_Printer_PrintMode", MODE_NEWFILE);
    if (file) {
        FPrintf(file, "%s\n", (ULONG)selected_print_mode);
        Close(file);
    }
}

// Load print mode from ENV:
void load_print_mode(void) {
    BPTR file = Open("ENV:IPP_Printer_PrintMode", MODE_OLDFILE);
    if (file) {
        char buffer[64];
        if (FGets(file, buffer, sizeof(buffer))) {
            buffer[strcspn(buffer, "\r\n")] = 0;  // Strip newline
            strncpy(selected_print_mode, buffer, MAX_ATTR_LEN - 1);
            selected_print_mode[MAX_ATTR_LEN - 1] = '\0';

            // Match it back to the index
            for (int i = 0; i < num_supported_print_modes; i++) {
                if (strcmp(supported_print_modes[i], selected_print_mode) == 0) {
                    print_mode = i;
                    break;
                }
            }
        }
        Close(file);
    }
}


static struct Gadget *find_gadget_by_id(UWORD id) {
    struct Gadget *g = glist;
    while (g && g->GadgetID != id) g = g->NextGadget;
    return g;
}

static void trim_config_line(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
                 s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static BOOL ensure_config_dir(CONST_STRPTR name) {
    BPTR lock;

    lock = Lock(name, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }

    lock = CreateDir(name);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

static void unit_config_path(int idx, BOOL envarc, char *out, int out_size) {
    snprintf(out, out_size, "%s:MintPRINT/Unit%d", envarc ? "ENVARC" : "ENV", idx);
}

static void unit_cache_path(int idx, BOOL envarc, char *out, int out_size) {
    snprintf(out, out_size, "%s:MintPRINT/Unit%d.cache", envarc ? "ENVARC" : "ENV", idx);
}

static BOOL unit_file_exists(int idx) {
    BPTR lock;
    char path[64];

    unit_config_path(idx, FALSE, path, sizeof(path));
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) {
        unit_config_path(idx, TRUE, path, sizeof(path));
        lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    }
    if (lock) {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
}

/* Peeks just the MODEL= line out of a saved unit file, without disturbing
 * any of the live GUI/driver-config state. Used to label the Unit dropdown. */
static void peek_unit_model(int idx, char *out, int out_size) {
    BPTR file;
    char path[64];
    char line[192];

    out[0] = '\0';

    unit_config_path(idx, FALSE, path, sizeof(path));
    file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!file) {
        unit_config_path(idx, TRUE, path, sizeof(path));
        file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    }
    if (!file) return;

    while (FGets(file, line, sizeof(line))) {
        trim_config_line(line);
        if (strncmp(line, "MODEL=", 6) == 0 && line[6]) {
            strncpy(out, line + 6, out_size - 1);
            out[out_size - 1] = '\0';
            break;
        }
    }
    Close(file);
}

/* Rebuilds the Unit dropdown's labels from whatever is currently saved on
 * disk for each slot ("Unit0 - Brother HL-L2350DW", "Unit1 (empty)", ...).
 * Callable before the window exists (win == NULL) to seed the gadget's
 * initial GTCY_Labels, or afterwards to refresh a live gadget - e.g. after
 * Save, in case a freshly-queried make/model just got written out. Matches
 * this file's existing "leak the old label block rather than free it while
 * GadTools might still reference it" convention (see update_media_dropdown
 * and friends). */
static void refresh_unit_dropdown(struct Window *win) {
    int i;

    for (i = 0; i < MAX_UNITS; i++) {
        char model[96];

        mp_unit_label_ptrs[i] = mp_unit_label_storage[i];
        mp_unit_label_storage[i][0] = '\0';

        if (i == current_unit_index && printer_make_model[0]) {
            strncpy(model, printer_make_model, sizeof(model) - 1);
            model[sizeof(model) - 1] = '\0';
        } else {
            peek_unit_model(i, model, sizeof(model));
        }

        /* The "Unit:" gadget label already says "Unit" - repeating it in
         * every cycle entry ("Unit0 - Brother MFC-J6930DW") just wastes
         * width that a real model name badly needs. */
        if (model[0]) {
            snprintf(mp_unit_label_storage[i], MP_UNIT_LABEL_LEN,
                     "%d: %s", i, model);
        } else if (unit_file_exists(i)) {
            snprintf(mp_unit_label_storage[i], MP_UNIT_LABEL_LEN,
                     "%d", i);
        } else {
            snprintf(mp_unit_label_storage[i], MP_UNIT_LABEL_LEN,
                     "%d (empty)", i);
        }
    }
    mp_unit_label_ptrs[MAX_UNITS] = NULL;
    unit_dropdown_labels = mp_unit_label_ptrs;

    if (win) {
        struct Gadget *g = find_gadget_by_id(GAD_UNIT_DROPDOWN);
        if (g) {
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)unit_dropdown_labels,
                              GTCY_Active, (ULONG)current_unit_index,
                              TAG_DONE);
            RefreshGList(g, win, NULL, 1);
            GT_RefreshWindow(win, NULL);
        }
    }
}

static const char *engine_mime_type(const char *engine) {
    if (strcmp(engine, "pwg-raster") == 0) return "image/pwg-raster";
    if (strcmp(engine, "pdf") == 0) return "application/pdf";
    return "image/jpeg";
}

/* engine_labels[] order: 0=JPEG, 1=PWG Raster, 2=PDF. */
static ULONG mp_engine_active_index(void) {
    int i;

    for (i = 0; i < mp_engine_count; ++i) {
        if (mp_engine_value_map[i] &&
            strcmp(driver_engine_buffer, mp_engine_value_map[i]) == 0)
            return (ULONG)i;
    }
    return 0;
}

/* Every document-format this driver's engines can actually produce. Kept
 * in sync with engine_mime_type()'s cases. */
static const char *mp_supported_engine_mimes[] = {
    "image/jpeg", "image/pwg-raster", "application/pdf"
};
#define MP_SUPPORTED_ENGINE_MIME_COUNT \
    (sizeof(mp_supported_engine_mimes) / sizeof(mp_supported_engine_mimes[0]))

/* After a successful Query, checks whether the printer advertised ANY
 * document-format this driver can actually produce. Unlike
 * warn_if_engine_unsupported() (which only flags a mismatch with the
 * currently-selected engine and is purely informational), a printer that
 * supports none of them cannot be printed to at all - a hard "this
 * printer isn't supported" finding, worth a real requester rather than a
 * status-box line easily missed among the rest of the Query output. */
static void mp_check_any_engine_supported(struct Window *win) {
    int i, j;
    BOOL any_match = FALSE;
    struct EasyStruct es;

    if (num_supported_formats == 0) return; /* printer didn't report - can't judge */

    for (i = 0; i < num_supported_formats && !any_match; i++) {
        for (j = 0; j < (int)MP_SUPPORTED_ENGINE_MIME_COUNT; j++) {
            if (strcasecmp(supported_formats[i], mp_supported_engine_mimes[j]) == 0) {
                any_match = TRUE;
                break;
            }
        }
    }

    if (any_match) return;

    es.es_StructSize = sizeof(struct EasyStruct);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"MintPrint Settings";
    es.es_TextFormat = (UBYTE *)
        "This printer did not advertise any document format\n"
        "MintPRINT can produce (JPEG, PWG Raster, or PDF).\n\n"
        "It is likely not supported yet. To help add support,\n"
        "please log an issue at github.com/boingball/MintPRINT -\n"
        "run windows_ipp_probe.py (from a Windows PC on the same\n"
        "network) against this printer and attach its output.";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(win, &es, NULL);
}

/* Cross-checks the chosen engine against the formats the printer actually
 * advertised in document-format-supported (populated by a prior Query).
 * Purely informational: it does not block Save, since a printer that has
 * never been queried yet has an empty list and should not be warned about. */
static void warn_if_engine_unsupported(const char *engine) {
    const char *mime;
    int i;
    BOOL found;

    if (num_supported_formats == 0) return;

    mime = engine_mime_type(engine);
    found = FALSE;
    for (i = 0; i < num_supported_formats; i++) {
        if (strcasecmp(supported_formats[i], mime) == 0) {
            found = TRUE;
            break;
        }
    }
    if (!found) {
        custom_printf("Warning: printer did not advertise %s support for the '%s' engine\n", mime, engine);
    }
}

static void capture_driver_settings(struct Window *win) {
    struct Gadget *g;
    char *value = NULL;
    ULONG active = driver_keep_job ? 1UL : 0UL;

    if (!win) return;

    g = find_gadget_by_id(GAD_IP_STRING);
    if (g && GT_GetGadgetAttrs(g, win, NULL,
                               GTST_String, (ULONG)&value,
                               TAG_DONE) && value) {
        strncpy(ip_buffer, value, sizeof(ip_buffer) - 1);
        ip_buffer[sizeof(ip_buffer) - 1] = '\0';
    }

    value = NULL;
    g = find_gadget_by_id(GAD_IPP_PATH);
    if (g && GT_GetGadgetAttrs(g, win, NULL,
                               GTST_String, (ULONG)&value,
                               TAG_DONE) && value) {
        strncpy(driver_path_buffer, value, sizeof(driver_path_buffer) - 1);
        driver_path_buffer[sizeof(driver_path_buffer) - 1] = '\0';
    }

    g = find_gadget_by_id(GAD_KEEPJOB);
    if (g) {
        GT_GetGadgetAttrs(g, win, NULL,
                          GTCY_Active, (ULONG)&active,
                          TAG_DONE);
        driver_keep_job = active ? TRUE : FALSE;
    }

    g = find_gadget_by_id(GAD_ENGINE);
    if (g) {
        ULONG engine_active = 0;
        GT_GetGadgetAttrs(g, win, NULL,
                          GTCY_Active, (ULONG)&engine_active,
                          TAG_DONE);
        if (engine_active < (ULONG)mp_engine_count &&
            mp_engine_value_map[engine_active]) {
            strncpy(driver_engine_buffer, mp_engine_value_map[engine_active],
                    sizeof(driver_engine_buffer) - 1);
            driver_engine_buffer[sizeof(driver_engine_buffer) - 1] = '\0';
        }
        warn_if_engine_unsupported(driver_engine_buffer);
    }

    /* Persist the capability-backed choices currently visible in the GUI. */
    if (media_dropdown && num_media_tray_mappings > 0) {
        ULONG selected = 0;
        GT_GetGadgetAttrs(media_dropdown, win, NULL,
                          GTCY_Active, (ULONG)&selected,
                          TAG_DONE);
        if (selected < (ULONG)num_media_tray_mappings) {
            strncpy(driver_media_buffer, media_tray_map[selected].media,
                    sizeof(driver_media_buffer) - 1);
            driver_media_buffer[sizeof(driver_media_buffer) - 1] = '\0';
            strncpy(driver_source_buffer, media_tray_map[selected].source,
                    sizeof(driver_source_buffer) - 1);
            driver_source_buffer[sizeof(driver_source_buffer) - 1] = '\0';
        }
    }

    g = find_gadget_by_id(GAD_PRINT_MODE);
    if (g && num_supported_print_modes > 0) {
        ULONG selected = 0;
        GT_GetGadgetAttrs(g, win, NULL,
                          GTCY_Active, (ULONG)&selected,
                          TAG_DONE);
        if (selected < (ULONG)num_supported_print_modes) {
            strncpy(driver_color_buffer, supported_print_modes[selected],
                    sizeof(driver_color_buffer) - 1);
            driver_color_buffer[sizeof(driver_color_buffer) - 1] = '\0';
            strncpy(selected_print_mode, supported_print_modes[selected],
                    sizeof(selected_print_mode) - 1);
            selected_print_mode[sizeof(selected_print_mode) - 1] = '\0';
        }
    }

    g = find_gadget_by_id(GAD_SCALING_MODE);
    if (g && num_supported_scaling > 0) {
        ULONG selected = 0;
        GT_GetGadgetAttrs(g, win, NULL,
                          GTCY_Active, (ULONG)&selected,
                          TAG_DONE);
        if (selected < (ULONG)num_supported_scaling) {
            strncpy(driver_scaling_buffer, supported_scaling[selected],
                    sizeof(driver_scaling_buffer) - 1);
            driver_scaling_buffer[sizeof(driver_scaling_buffer) - 1] = '\0';
            strncpy(selected_scaling, supported_scaling[selected],
                    sizeof(selected_scaling) - 1);
            selected_scaling[sizeof(selected_scaling) - 1] = '\0';
        }
    }

    g = find_gadget_by_id(GAD_QUALITY_MODE);
    if (g && num_supported_quality > 0) {
        ULONG selected = 0;
        GT_GetGadgetAttrs(g, win, NULL,
                          GTCY_Active, (ULONG)&selected,
                          TAG_DONE);
        if (selected < (ULONG)num_supported_quality) {
            strncpy(driver_quality_buffer, supported_quality[selected],
                    sizeof(driver_quality_buffer) - 1);
            driver_quality_buffer[sizeof(driver_quality_buffer) - 1] = '\0';
            strncpy(selected_quality, supported_quality[selected],
                    sizeof(selected_quality) - 1);
            selected_quality[sizeof(selected_quality) - 1] = '\0';
        }
    }
}

static BOOL write_driver_config_file(CONST_STRPTR filename) {
    BPTR file;
    char host[64];
    int port = -1;
    char line[192];

    if (!parse_ip_and_port(ip_buffer, host, sizeof(host), &port) || !host[0]) {
        printf("Invalid printer host/IP: %s\n", ip_buffer);
        return FALSE;
    }
    if (port <= 0) port = 80;
    if (port > 65535) {
        printf("Invalid printer port: %d\n", port);
        return FALSE;
    }
    if (!driver_path_buffer[0] || driver_path_buffer[0] != '/') {
        printf("IPP path must start with '/': %s\n", driver_path_buffer);
        return FALSE;
    }

    file = Open(filename, MODE_NEWFILE);
    if (!file) return FALSE;

    snprintf(line, sizeof(line), "# MintPRINT Unit%d - written by MintPrint Settings\n", current_unit_index);
    FPuts(file, line);
    snprintf(line, sizeof(line), "HOST=%s\n", host);
    FPuts(file, line);
    snprintf(line, sizeof(line), "PORT=%d\n", port);
    FPuts(file, line);
    snprintf(line, sizeof(line), "PATH=%s\n", driver_path_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "ENGINE=%s\n", driver_engine_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "KEEPJOB=%d\n", driver_keep_job ? 1 : 0);
    FPuts(file, line);
    snprintf(line, sizeof(line), "MEDIA=%s\n", driver_media_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "SOURCE=%s\n", driver_source_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "COLOR=%s\n", driver_color_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "QUALITY=%s\n", driver_quality_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "SCALING=%s\n", driver_scaling_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "SIDES=%s\n", driver_sides_buffer);
    FPuts(file, line);
    snprintf(line, sizeof(line), "MODEL=%s\n", printer_make_model);
    FPuts(file, line);
    Close(file);
    return TRUE;
}

static BOOL save_driver_config(struct Window *win) {
    BOOL env_ok;
    BOOL envarc_ok;
    char env_path[64];
    char envarc_path[64];

    capture_driver_settings(win);

    if (!ensure_config_dir((CONST_STRPTR)"ENV:MintPRINT")) {
        printf("Could not create/find ENV:MintPRINT\n");
        return FALSE;
    }
    if (!ensure_config_dir((CONST_STRPTR)"ENVARC:MintPRINT")) {
        printf("Could not create/find ENVARC:MintPRINT\n");
        return FALSE;
    }

    unit_config_path(current_unit_index, FALSE, env_path, sizeof(env_path));
    unit_config_path(current_unit_index, TRUE, envarc_path, sizeof(envarc_path));

    env_ok = write_driver_config_file((CONST_STRPTR)env_path);
    envarc_ok = write_driver_config_file((CONST_STRPTR)envarc_path);

    /*
     * Do NOT replace the live Unit cycle gadget's GTCY_Labels here.
     *
     * On classic GadTools (OS3.1/V37), repeatedly swapping a CYCLE_KIND
     * label array while the gadget is live can leave internal gadget state
     * pointing at the old label list.  The failure shows up later during
     * GUI teardown (Save -> Exit can hard-lock the machine), not necessarily
     * at the GT_SetGadgetAttrs() call itself.
     *
     * Saving does not actually require a Unit dropdown rebuild: the current
     * Unit number is unchanged, and a freshly queried model is already
     * previewed by the query path.  Leave the existing live labels alone.
     * They will be rebuilt normally the next time Settings is launched.
     */
    (void)win;

    return env_ok && envarc_ok;
}

static BOOL load_driver_config(void) {
    BPTR file;
    char line[192];
    char host[64] = "192.168.0.51";
    char env_path[64];
    char envarc_path[64];
    int port = 80;
    BOOL found = FALSE;

    strcpy(driver_path_buffer, "/ipp/print");
    strcpy(driver_engine_buffer, "jpeg");
    driver_keep_job = TRUE;
    driver_media_buffer[0] = '\0';
    driver_source_buffer[0] = '\0';
    driver_color_buffer[0] = '\0';
    driver_quality_buffer[0] = '\0';
    driver_scaling_buffer[0] = '\0';
    driver_sides_buffer[0] = '\0';
    printer_make_model[0] = '\0';

    unit_config_path(current_unit_index, FALSE, env_path, sizeof(env_path));
    unit_config_path(current_unit_index, TRUE, envarc_path, sizeof(envarc_path));

    file = Open((CONST_STRPTR)env_path, MODE_OLDFILE);
    if (!file)
        file = Open((CONST_STRPTR)envarc_path, MODE_OLDFILE);

    if (!file) {
        snprintf(ip_buffer, sizeof(ip_buffer), "%s:%d", host, port);
        return FALSE;
    }

    found = TRUE;
    while (FGets(file, line, sizeof(line))) {
        char *value;
        trim_config_line(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        if (strncmp(line, "HOST=", 5) == 0) {
            value = line + 5;
            if (*value) {
                strncpy(host, value, sizeof(host) - 1);
                host[sizeof(host) - 1] = '\0';
            }
        } else if (strncmp(line, "PORT=", 5) == 0) {
            int parsed = atoi(line + 5);
            if (parsed >= 1 && parsed <= 65535) port = parsed;
        } else if (strncmp(line, "PATH=", 5) == 0) {
            value = line + 5;
            if (*value == '/') {
                strncpy(driver_path_buffer, value, sizeof(driver_path_buffer) - 1);
                driver_path_buffer[sizeof(driver_path_buffer) - 1] = '\0';
            }
        } else if (strncmp(line, "ENGINE=", 7) == 0) {
            if (strcmp(line + 7, "pwg-raster") == 0)
                strcpy(driver_engine_buffer, "pwg-raster");
            else if (strcmp(line + 7, "pdf") == 0)
                strcpy(driver_engine_buffer, "pdf");
            else
                strcpy(driver_engine_buffer, "jpeg");
        } else if (strncmp(line, "KEEPJOB=", 8) == 0) {
            driver_keep_job = (line[8] == '0') ? FALSE : TRUE;
        } else if (strncmp(line, "MEDIA=", 6) == 0) {
            strncpy(driver_media_buffer, line + 6, sizeof(driver_media_buffer) - 1);
            driver_media_buffer[sizeof(driver_media_buffer) - 1] = '\0';
        } else if (strncmp(line, "SOURCE=", 7) == 0) {
            strncpy(driver_source_buffer, line + 7, sizeof(driver_source_buffer) - 1);
            driver_source_buffer[sizeof(driver_source_buffer) - 1] = '\0';
        } else if (strncmp(line, "COLOR=", 6) == 0) {
            strncpy(driver_color_buffer, line + 6, sizeof(driver_color_buffer) - 1);
            driver_color_buffer[sizeof(driver_color_buffer) - 1] = '\0';
        } else if (strncmp(line, "QUALITY=", 8) == 0) {
            strncpy(driver_quality_buffer, line + 8, sizeof(driver_quality_buffer) - 1);
            driver_quality_buffer[sizeof(driver_quality_buffer) - 1] = '\0';
        } else if (strncmp(line, "SCALING=", 8) == 0) {
            strncpy(driver_scaling_buffer, line + 8, sizeof(driver_scaling_buffer) - 1);
            driver_scaling_buffer[sizeof(driver_scaling_buffer) - 1] = '\0';
        } else if (strncmp(line, "SIDES=", 6) == 0) {
            strncpy(driver_sides_buffer, line + 6, sizeof(driver_sides_buffer) - 1);
            driver_sides_buffer[sizeof(driver_sides_buffer) - 1] = '\0';
        } else if (strncmp(line, "MODEL=", 6) == 0) {
            strncpy(printer_make_model, line + 6, sizeof(printer_make_model) - 1);
            printer_make_model[sizeof(printer_make_model) - 1] = '\0';
        }
    }

    Close(file);
    snprintf(ip_buffer, sizeof(ip_buffer), "%s:%d", host, port);
    return found;
}

static void seed_saved_option_labels(void) {
    if (driver_media_buffer[0]) {
        if (driver_source_buffer[0])
            snprintf(initial_media_value, sizeof(initial_media_value), "%s (%s)",
                     driver_media_buffer, driver_source_buffer);
        else
            snprintf(initial_media_value, sizeof(initial_media_value), "%s",
                     driver_media_buffer);
    } else {
        strcpy(initial_media_value, "Not Detected");
    }

    if (driver_color_buffer[0])
        snprintf(initial_print_mode_value, sizeof(initial_print_mode_value), "%s",
                 driver_color_buffer);
    else
        strcpy(initial_print_mode_value, "Not Detected");

    if (driver_scaling_buffer[0])
        snprintf(initial_scaling_value, sizeof(initial_scaling_value), "%s",
                 driver_scaling_buffer);
    else
        strcpy(initial_scaling_value, "Not Detected");

    if (driver_quality_buffer[0])
        snprintf(initial_quality_value, sizeof(initial_quality_value), "%s",
                 driver_quality_buffer);
    else
        strcpy(initial_quality_value, "Not Detected");

    mp_media_label_ptrs[0] = mp_media_label_storage[0];
    strncpy(mp_media_label_storage[0], initial_media_value,
            sizeof(mp_media_label_storage[0]) - 1);
    mp_media_label_storage[0][sizeof(mp_media_label_storage[0]) - 1] = '\0';
    mp_media_label_ptrs[1] = NULL;

    mp_print_mode_label_ptrs[0] = mp_print_mode_label_storage[0];
    strncpy(mp_print_mode_label_storage[0], initial_print_mode_value,
            sizeof(mp_print_mode_label_storage[0]) - 1);
    mp_print_mode_label_storage[0][sizeof(mp_print_mode_label_storage[0]) - 1] = '\0';
    mp_print_mode_label_ptrs[1] = NULL;

    mp_scaling_label_ptrs[0] = mp_scaling_label_storage[0];
    strncpy(mp_scaling_label_storage[0], initial_scaling_value,
            sizeof(mp_scaling_label_storage[0]) - 1);
    mp_scaling_label_storage[0][sizeof(mp_scaling_label_storage[0]) - 1] = '\0';
    mp_scaling_label_ptrs[1] = NULL;

    mp_quality_label_ptrs[0] = mp_quality_label_storage[0];
    strncpy(mp_quality_label_storage[0], initial_quality_value,
            sizeof(mp_quality_label_storage[0]) - 1);
    mp_quality_label_storage[0][sizeof(mp_quality_label_storage[0]) - 1] = '\0';
    mp_quality_label_ptrs[1] = NULL;

    media_dropdown_items = mp_media_label_ptrs;
    print_mode_labels = mp_print_mode_label_ptrs;
    scaling_mode_labels = mp_scaling_label_ptrs;
    quality_mode_labels = mp_quality_label_ptrs;
}

static void apply_saved_option_state(struct Window *win) {
    struct Gadget *g;

    if (!win) return;
    seed_saved_option_labels();

    if (num_media_tray_mappings == 0) {
        g = find_gadget_by_id(GAD_MEDIA_DROPDOWN);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)media_dropdown_items,
                              GTCY_Active, 0,
                              GA_Disabled, driver_media_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_scaling == 0) {
        g = find_gadget_by_id(GAD_SCALING_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)scaling_mode_labels,
                              GTCY_Active, 0,
                              GA_Disabled, driver_scaling_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_quality == 0) {
        g = find_gadget_by_id(GAD_QUALITY_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)quality_mode_labels,
                              GTCY_Active, 0,
                              GA_Disabled, driver_quality_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_print_modes == 0) {
        g = find_gadget_by_id(GAD_PRINT_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)print_mode_labels,
                              GTCY_Active, 0,
                              GA_Disabled, driver_color_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    GT_RefreshWindow(win, NULL);
}

static void apply_job_defaults_to_gadgets(struct Window *win) {
    struct Gadget *g;
    int i;

    if (!win) return;

    if (media_dropdown && num_media_tray_mappings > 0 && driver_media_buffer[0]) {
        for (i = 0; i < num_media_tray_mappings; ++i) {
            if (strcmp(media_tray_map[i].media, driver_media_buffer) == 0 &&
                (!driver_source_buffer[0] ||
                 strcmp(media_tray_map[i].source, driver_source_buffer) == 0)) {
                GT_SetGadgetAttrs(media_dropdown, win, NULL,
                                  GTCY_Active, (ULONG)i,
                                  TAG_DONE);
                break;
            }
        }
    }

    g = find_gadget_by_id(GAD_PRINT_MODE);
    if (g && driver_color_buffer[0]) {
        for (i = 0; i < num_supported_print_modes; ++i) {
            if (strcmp(supported_print_modes[i], driver_color_buffer) == 0) {
                GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)i, TAG_DONE);
                strncpy(selected_print_mode, supported_print_modes[i],
                        sizeof(selected_print_mode) - 1);
                selected_print_mode[sizeof(selected_print_mode) - 1] = '\0';
                break;
            }
        }
    }

    g = find_gadget_by_id(GAD_SCALING_MODE);
    if (g && driver_scaling_buffer[0]) {
        for (i = 0; i < num_supported_scaling; ++i) {
            if (strcmp(supported_scaling[i], driver_scaling_buffer) == 0) {
                GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)i, TAG_DONE);
                strncpy(selected_scaling, supported_scaling[i], sizeof(selected_scaling) - 1);
                selected_scaling[sizeof(selected_scaling) - 1] = '\0';
                break;
            }
        }
    }

    g = find_gadget_by_id(GAD_QUALITY_MODE);
    if (g && driver_quality_buffer[0]) {
        for (i = 0; i < num_supported_quality; ++i) {
            if (strcmp(supported_quality[i], driver_quality_buffer) == 0) {
                GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)i, TAG_DONE);
                strncpy(selected_quality, supported_quality[i], sizeof(selected_quality) - 1);
                selected_quality[sizeof(selected_quality) - 1] = '\0';
                break;
            }
        }
    }

    GT_RefreshWindow(win, NULL);
}

static BOOL mintprint_test_page(struct Window *win) {
    struct MsgPort *mp = NULL;
    struct IODRPReq *req = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp;
    ULONG mode_id = 0;
    UBYTE depth;
    LONG ioerr = -1;
    BOOL print_ok = FALSE;
    const char *title = "MintPRINT TEST PAGE";
    const char *line2 = "printer.device -> MintPRINT -> IPP";
    const char *line3 = "Capability-backed Unit0 defaults";
    const char *line4 = "Media / tray / colour / quality / scaling";

    if (!screen) {
        printf("Test Print: public screen is not available\n");
        return FALSE;
    }

    /* Test the settings the user is looking at, and make them the live Unit0. */
    if (!save_driver_config(win)) {
        printf("Test Print: could not save Unit0 settings\n");
        return FALSE;
    }

    depth = screen->RastPort.BitMap->Depth;
    if (depth < 1) depth = 1;

    bm = AllocBitMap(320, 180, depth, BMF_CLEAR, screen->RastPort.BitMap);
    if (!bm) {
        printf("Test Print: could not allocate test bitmap\n");
        return FALSE;
    }

    InitRastPort(&rp);
    rp.BitMap = bm;
    if (screen->RastPort.Font) SetFont(&rp, screen->RastPort.Font);

    /* Workbench palette pens are intentional: it makes a simple colour test. */
    SetAPen(&rp, 0);
    RectFill(&rp, 0, 0, 319, 179);
    SetAPen(&rp, 1);
    RectFill(&rp, 4, 4, 315, 5);
    RectFill(&rp, 4, 174, 315, 175);
    RectFill(&rp, 4, 4, 5, 175);
    RectFill(&rp, 314, 4, 315, 175);
    Move(&rp, 16, 24);
    Text(&rp, (STRPTR)title, strlen(title));
    Move(&rp, 16, 40);
    Text(&rp, (STRPTR)line2, strlen(line2));

    SetAPen(&rp, depth > 1 ? 2 : 1);
    RectFill(&rp, 16, 60, 105, 105);
    SetAPen(&rp, depth > 1 ? 3 : 1);
    RectFill(&rp, 115, 60, 204, 105);
    SetAPen(&rp, 1);
    RectFill(&rp, 214, 60, 303, 105);

    SetAPen(&rp, 1);
    Move(&rp, 16, 130);
    Text(&rp, (STRPTR)line3, strlen(line3));
    Move(&rp, 16, 146);
    Text(&rp, (STRPTR)line4, strlen(line4));

    mp = CreateMsgPort();
    if (!mp) {
        printf("Test Print: CreateMsgPort failed\n");
        FreeBitMap(bm);
        return FALSE;
    }

    req = (struct IODRPReq *)CreateIORequest(mp, sizeof(struct IODRPReq));
    if (!req) {
        printf("Test Print: CreateIORequest failed\n");
        DeleteMsgPort(mp);
        FreeBitMap(bm);
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)"printer.device", 0,
                   (struct IORequest *)req, 0) != 0) {
        printf("Test Print: could not open printer.device\n");
        DeleteIORequest((struct IORequest *)req);
        DeleteMsgPort(mp);
        FreeBitMap(bm);
        return FALSE;
    }

    mode_id = GetVPModeID(&screen->ViewPort);
    if (mode_id == INVALID_ID) mode_id = 0;

    req->io_Command = PRD_DUMPRPORT;
    req->io_RastPort = &rp;
    req->io_ColorMap = screen->ViewPort.ColorMap;
    req->io_Modes = mode_id;
    req->io_SrcX = 0;
    req->io_SrcY = 0;
    req->io_SrcWidth = 320;
    req->io_SrcHeight = 180;
    req->io_DestCols = 0;
    req->io_DestRows = 0;
    req->io_Special = SPECIAL_ASPECT | SPECIAL_CENTER;

    printf("Test Print: sending page through printer.device...\n");
    ioerr = DoIO((struct IORequest *)req);
    if (ioerr != 0 || req->io_Error != 0) {
        printf("Test Print failed: DoIO=%ld io_Error=%ld\n",
               ioerr, (LONG)req->io_Error);
    } else {
        printf("Test Print completed successfully\n");
        print_ok = TRUE;
    }

    printf("Test Print cleanup: before CloseDevice\n");
    CloseDevice((struct IORequest *)req);
    printf("Test Print cleanup: after CloseDevice\n");

    DeleteIORequest((struct IORequest *)req);
    printf("Test Print cleanup: after DeleteIORequest\n");

    DeleteMsgPort(mp);
    printf("Test Print cleanup: after DeleteMsgPort\n");

    FreeBitMap(bm);
    printf("Test Print cleanup: after FreeBitMap\n");

    return print_ok;
}

static void apply_driver_config_to_gadgets(struct Window *win) {
    struct Gadget *g;

    if (!win) return;

    g = find_gadget_by_id(GAD_IP_STRING);
    if (g)
        GT_SetGadgetAttrs(g, win, NULL,
                          GTST_String, (ULONG)ip_buffer,
                          TAG_DONE);

    g = find_gadget_by_id(GAD_IPP_PATH);
    if (g)
        GT_SetGadgetAttrs(g, win, NULL,
                          GTST_String, (ULONG)driver_path_buffer,
                          TAG_DONE);

    g = find_gadget_by_id(GAD_KEEPJOB);
    if (g)
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Active, driver_keep_job ? 1 : 0,
                          TAG_DONE);

    g = find_gadget_by_id(GAD_ENGINE);
    if (g)
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Active, mp_engine_active_index(),
                          TAG_DONE);

    g = find_gadget_by_id(GAD_MODEL_DISPLAY);
    if (g)
        GT_SetGadgetAttrs(g, win, NULL,
                          GTST_String, (ULONG)printer_make_model,
                          TAG_DONE);

    GT_RefreshWindow(win, NULL);
}

// Add after successful query to rebuild media dropdown (Updated to show media (tray))
void update_media_dropdown(struct Window *win) {
    int i;
    int count = num_media_tray_mappings;

    printf("Updating media dropdown, num_mappings=%d\n", num_media_tray_mappings);

    if (count <= 0) {
        count = 1;
        mp_media_label_ptrs[0] = mp_media_label_storage[0];
        strcpy(mp_media_label_storage[0], "No Media Available");
    } else {
        if (count > MAX_VALUES) count = MAX_VALUES;
        for (i = 0; i < count; i++) {
            const char *media = media_tray_map[i].media[0]
                              ? media_tray_map[i].media : "Unknown";
            const char *tray = media_tray_map[i].trayName[0]
                             ? media_tray_map[i].trayName : "Unknown";

            mp_media_label_ptrs[i] = mp_media_label_storage[i];
            snprintf(mp_media_label_storage[i],
                     sizeof(mp_media_label_storage[i]),
                     "%s (%s)", media, tray);
            printf("Dropdown item %d: %s\n",
                   i, mp_media_label_storage[i]);
        }
    }

    mp_media_label_ptrs[count] = NULL;
    media_dropdown_items = mp_media_label_ptrs;

    if (media_dropdown && win) {
        GT_SetGadgetAttrs(media_dropdown, win, NULL,
                          GTCY_Labels, (ULONG)media_dropdown_items,
                          GTCY_Active, 0,
                          GA_Disabled, num_media_tray_mappings > 0 ? FALSE : TRUE,
                          TAG_DONE);
        RefreshGList(media_dropdown, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_scaling_dropdown(struct Window *win) {
    struct Gadget *g;
    int i;
    int count = num_supported_scaling;

    if (count <= 0) {
        count = 1;
        mp_scaling_label_ptrs[0] = mp_scaling_label_storage[0];
        strncpy(mp_scaling_label_storage[0], initial_scaling_value,
                sizeof(mp_scaling_label_storage[0]) - 1);
        mp_scaling_label_storage[0][sizeof(mp_scaling_label_storage[0]) - 1] = '\0';
    } else {
        if (count > MAX_VALUES) count = MAX_VALUES;
        for (i = 0; i < count; i++) {
            mp_scaling_label_ptrs[i] = mp_scaling_label_storage[i];
            strncpy(mp_scaling_label_storage[i], supported_scaling[i],
                    sizeof(mp_scaling_label_storage[i]) - 1);
            mp_scaling_label_storage[i][sizeof(mp_scaling_label_storage[i]) - 1] = '\0';
        }
    }
    mp_scaling_label_ptrs[count] = NULL;
    scaling_mode_labels = mp_scaling_label_ptrs;

    g = find_gadget_by_id(GAD_SCALING_MODE);
    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)scaling_mode_labels,
                          GTCY_Active, 0,
                          GA_Disabled, num_supported_scaling > 0 ? FALSE : TRUE,
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_print_mode_dropdown(struct Window *win) {
    struct Gadget *g;
    int i;
    int count = num_supported_print_modes;

    if (count <= 0) {
        count = 1;
        mp_print_mode_label_ptrs[0] = mp_print_mode_label_storage[0];
        strncpy(mp_print_mode_label_storage[0], initial_print_mode_value,
                sizeof(mp_print_mode_label_storage[0]) - 1);
        mp_print_mode_label_storage[0][sizeof(mp_print_mode_label_storage[0]) - 1] = '\0';
    } else {
        if (count > MAX_VALUES) count = MAX_VALUES;
        for (i = 0; i < count; i++) {
            mp_print_mode_label_ptrs[i] = mp_print_mode_label_storage[i];
            strncpy(mp_print_mode_label_storage[i], supported_print_modes[i],
                    sizeof(mp_print_mode_label_storage[i]) - 1);
            mp_print_mode_label_storage[i][sizeof(mp_print_mode_label_storage[i]) - 1] = '\0';
        }
    }
    mp_print_mode_label_ptrs[count] = NULL;
    print_mode_labels = mp_print_mode_label_ptrs;

    g = find_gadget_by_id(GAD_PRINT_MODE);
    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)print_mode_labels,
                          GTCY_Active, 0,
                          GA_Disabled, num_supported_print_modes > 0 ? FALSE : TRUE,
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_quality_dropdown(struct Window *win) {
    struct Gadget *g;
    int i;
    int count = num_supported_quality;

    if (count <= 0) {
        count = 1;
        mp_quality_label_ptrs[0] = mp_quality_label_storage[0];
        strncpy(mp_quality_label_storage[0], initial_quality_value,
                sizeof(mp_quality_label_storage[0]) - 1);
        mp_quality_label_storage[0][sizeof(mp_quality_label_storage[0]) - 1] = '\0';
    } else {
        if (count > MAX_VALUES) count = MAX_VALUES;
        for (i = 0; i < count; i++) {
            mp_quality_label_ptrs[i] = mp_quality_label_storage[i];
            strncpy(mp_quality_label_storage[i], supported_quality[i],
                    sizeof(mp_quality_label_storage[i]) - 1);
            mp_quality_label_storage[i][sizeof(mp_quality_label_storage[i]) - 1] = '\0';
        }
    }
    mp_quality_label_ptrs[count] = NULL;
    quality_mode_labels = mp_quality_label_ptrs;

    g = find_gadget_by_id(GAD_QUALITY_MODE);
    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)quality_mode_labels,
                          GTCY_Active, 0,
                          GA_Disabled, num_supported_quality > 0 ? FALSE : TRUE,
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

static BOOL mp_printer_advertises_format(const char *mime) {
    int i;

    if (!mime) return FALSE;
    for (i = 0; i < num_supported_formats; ++i) {
        if (strcasecmp(supported_formats[i], mime) == 0)
            return TRUE;
    }
    return FALSE;
}

/*
 * Rebuild the Engine Cycle from document-format-supported.
 *
 * With no Query/cache yet, all MintPRINT engines remain visible.
 * After a Query, only engines the printer actually advertised are shown.
 * If it advertised none of MintPRINT's formats, leave all three visible;
 * the existing unsupported-printer requester handles that exceptional case
 * and an empty GadTools Cycle would be undesirable.
 */
static void mp_rebuild_engine_options_from_query(void) {
    int i;
    int out = 0;
    BOOL use_query = num_supported_formats > 0;
    BOOL current_found = FALSE;

    for (i = 0; i < MP_ENGINE_MAX; ++i) {
        if (!use_query ||
            mp_printer_advertises_format(mp_engine_all_mimes[i])) {
            engine_labels[out] = (STRPTR)mp_engine_all_labels[i];
            mp_engine_value_map[out] = mp_engine_all_values[i];
            if (strcmp(driver_engine_buffer, mp_engine_all_values[i]) == 0)
                current_found = TRUE;
            ++out;
        }
    }

    if (out == 0) {
        for (i = 0; i < MP_ENGINE_MAX; ++i) {
            engine_labels[i] = (STRPTR)mp_engine_all_labels[i];
            mp_engine_value_map[i] = mp_engine_all_values[i];
        }
        out = MP_ENGINE_MAX;
        current_found = TRUE;
    }

    engine_labels[out] = NULL;
    mp_engine_count = out;

    if (!current_found && mp_engine_count > 0) {
        strncpy(driver_engine_buffer, mp_engine_value_map[0],
                sizeof(driver_engine_buffer) - 1);
        driver_engine_buffer[sizeof(driver_engine_buffer) - 1] = '\0';
    }
}

static void update_engine_dropdown(struct Window *win) {
    struct Gadget *g;
    char previous[sizeof(driver_engine_buffer)];

    strncpy(previous, driver_engine_buffer, sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';

    mp_rebuild_engine_options_from_query();

    if (!win) return;

    g = find_gadget_by_id(GAD_ENGINE);
    if (g) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)engine_labels,
                          GTCY_Active, mp_engine_active_index(),
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }

    if (strcmp(previous, driver_engine_buffer) != 0) {
        printf("Selected %s because the previous engine was not advertised by this printer.\n",
               engine_labels[mp_engine_active_index()]);
    }
}

void cleanup_dropdown_labels() {
    /*
     * All CYCLE_KIND label arrays and strings are static process-lifetime
     * storage. FreeGadgets() has already detached GadTools from them, and
     * there is intentionally nothing to FreeVec here.
     */
}

/* MintPRINT prefs #8: capability cache.
 *
 * Unit0 contains selected defaults.
 * Unit0.cache contains printer-discovered capabilities.
 * ENV: is preferred for the current session; ENVARC: makes the cache survive
 * reboot. A cache is only used when HOST/PORT/PATH match the current Unit0.
 */
#define MP_CAP_CACHE_LINE_MAX 384
static char mp_cap_cache_line[MP_CAP_CACHE_LINE_MAX];

static void mp_cache_copy(char *dst, int dst_size, const char *src) {
    if (!dst || dst_size <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void mp_cache_clear_capabilities(void) {
    num_supported_formats = 0;
    num_supported_media = 0;
    num_supported_output_modes = 0;
    num_supported_sides = 0;
    num_supported_scaling = 0;
    num_supported_orientations = 0;
    num_supported_media_sources = 0;
    num_supported_print_modes = 0;
    num_supported_quality = 0;
    num_media_tray_mappings = 0;
    has_media_ready = FALSE;
}

static BOOL mp_cache_write_file(CONST_STRPTR filename,
                                CONST_STRPTR host,
                                int port,
                                CONST_STRPTR path) {
    BPTR fh;
    char line[384];
    int i;

    fh = Open(filename, MODE_NEWFILE);
    if (!fh) return FALSE;

    FPuts(fh, "# MintPRINT printer capability cache\n");
    FPuts(fh, "CACHE_VERSION=1\n");

    snprintf(line, sizeof(line), "HOST=%s\n", host);
    FPuts(fh, line);
    snprintf(line, sizeof(line), "PORT=%d\n", port);
    FPuts(fh, line);
    snprintf(line, sizeof(line), "PATH=%s\n", path);
    FPuts(fh, line);

    for (i = 0; i < num_supported_formats; ++i) {
        snprintf(line, sizeof(line), "FORMAT=%s\n", supported_formats[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_media; ++i) {
        snprintf(line, sizeof(line), "MEDIA_SUPPORTED=%s\n", supported_media[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_media_sources; ++i) {
        snprintf(line, sizeof(line), "SOURCE=%s\n", supported_media_sources[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_media_tray_mappings; ++i) {
        snprintf(line, sizeof(line), "MEDIA=%s|%s|%s|%s\n",
                 media_tray_map[i].media,
                 media_tray_map[i].source,
                 media_tray_map[i].trayName,
                 media_tray_map[i].medianame);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_output_modes; ++i) {
        snprintf(line, sizeof(line), "OUTPUTMODE=%s\n", supported_output_modes[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_sides; ++i) {
        snprintf(line, sizeof(line), "SIDE=%s\n", supported_sides[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_scaling; ++i) {
        snprintf(line, sizeof(line), "SCALING=%s\n", supported_scaling[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_orientations; ++i) {
        snprintf(line, sizeof(line), "ORIENTATION=%d\n", supported_orientations[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_print_modes; ++i) {
        snprintf(line, sizeof(line), "PRINTMODE=%s\n", supported_print_modes[i]);
        FPuts(fh, line);
    }

    for (i = 0; i < num_supported_quality; ++i) {
        snprintf(line, sizeof(line), "QUALITY=%s\n", supported_quality[i]);
        FPuts(fh, line);
    }

    Close(fh);
    return TRUE;
}

static BOOL save_capability_cache(CONST_STRPTR host, int port, CONST_STRPTR path) {
    BOOL env_ok;
    BOOL envarc_ok;
    char env_cache[64];
    char envarc_cache[64];

    if (!host || !host[0] || port <= 0 || port > 65535 ||
        !path || path[0] != '/') {
        return FALSE;
    }

    if (!ensure_config_dir((CONST_STRPTR)"ENV:MintPRINT"))
        return FALSE;
    if (!ensure_config_dir((CONST_STRPTR)"ENVARC:MintPRINT"))
        return FALSE;

    unit_cache_path(current_unit_index, FALSE, env_cache, sizeof(env_cache));
    unit_cache_path(current_unit_index, TRUE, envarc_cache, sizeof(envarc_cache));

    env_ok = mp_cache_write_file((CONST_STRPTR)env_cache, host, port, path);
    envarc_ok = mp_cache_write_file((CONST_STRPTR)envarc_cache, host, port, path);

    return env_ok && envarc_ok;
}

static BOOL mp_cache_endpoint_matches(CONST_STRPTR filename,
                                      CONST_STRPTR expected_host,
                                      int expected_port,
                                      CONST_STRPTR expected_path) {
    BPTR fh;
    char host[64] = "";
    char path[96] = "";
    int port = -1;

    fh = Open(filename, MODE_OLDFILE);
    if (!fh) return FALSE;

    while (FGets(fh, (STRPTR)mp_cap_cache_line, sizeof(mp_cap_cache_line))) {
        trim_config_line(mp_cap_cache_line);

        if (strncmp(mp_cap_cache_line, "HOST=", 5) == 0) {
            mp_cache_copy(host, sizeof(host), mp_cap_cache_line + 5);
        } else if (strncmp(mp_cap_cache_line, "PORT=", 5) == 0) {
            port = atoi(mp_cap_cache_line + 5);
        } else if (strncmp(mp_cap_cache_line, "PATH=", 5) == 0) {
            mp_cache_copy(path, sizeof(path), mp_cap_cache_line + 5);
        }
    }

    Close(fh);

    return strcmp(host, expected_host) == 0 &&
           port == expected_port &&
           strcmp(path, expected_path) == 0;
}

static void mp_cache_parse_media(char *value) {
    char *p1;
    char *p2;
    char *p3;
    int i;

    if (num_media_tray_mappings >= MAX_VALUES) return;

    p1 = strchr(value, '|');
    if (!p1) return;
    *p1++ = '\0';

    p2 = strchr(p1, '|');
    if (!p2) return;
    *p2++ = '\0';

    p3 = strchr(p2, '|');
    if (!p3) return;
    *p3++ = '\0';

    i = num_media_tray_mappings;
    mp_cache_copy(media_tray_map[i].media,
                  sizeof(media_tray_map[i].media), value);
    mp_cache_copy(media_tray_map[i].source,
                  sizeof(media_tray_map[i].source), p1);
    mp_cache_copy(media_tray_map[i].trayName,
                  sizeof(media_tray_map[i].trayName), p2);
    mp_cache_copy(media_tray_map[i].medianame,
                  sizeof(media_tray_map[i].medianame), p3);
    num_media_tray_mappings++;
}

static BOOL mp_cache_load_file(CONST_STRPTR filename) {
    BPTR fh;

    fh = Open(filename, MODE_OLDFILE);
    if (!fh) return FALSE;

    mp_cache_clear_capabilities();

    while (FGets(fh, (STRPTR)mp_cap_cache_line, sizeof(mp_cap_cache_line))) {
        char *value;

        trim_config_line(mp_cap_cache_line);
        if (!mp_cap_cache_line[0] ||
            mp_cap_cache_line[0] == '#' ||
            mp_cap_cache_line[0] == ';') {
            continue;
        }

        if (strncmp(mp_cap_cache_line, "FORMAT=", 7) == 0) {
            store_value(supported_formats, &num_supported_formats,
                        mp_cap_cache_line + 7);
        } else if (strncmp(mp_cap_cache_line, "MEDIA_SUPPORTED=", 16) == 0) {
            store_value(supported_media, &num_supported_media,
                        mp_cap_cache_line + 16);
        } else if (strncmp(mp_cap_cache_line, "SOURCE=", 7) == 0) {
            store_value(supported_media_sources, &num_supported_media_sources,
                        mp_cap_cache_line + 7);
        } else if (strncmp(mp_cap_cache_line, "MEDIA=", 6) == 0) {
            value = mp_cap_cache_line + 6;
            mp_cache_parse_media(value);
        } else if (strncmp(mp_cap_cache_line, "OUTPUTMODE=", 11) == 0) {
            store_value(supported_output_modes, &num_supported_output_modes,
                        mp_cap_cache_line + 11);
        } else if (strncmp(mp_cap_cache_line, "SIDE=", 5) == 0) {
            store_value(supported_sides, &num_supported_sides,
                        mp_cap_cache_line + 5);
        } else if (strncmp(mp_cap_cache_line, "SCALING=", 8) == 0) {
            store_value(supported_scaling, &num_supported_scaling,
                        mp_cap_cache_line + 8);
        } else if (strncmp(mp_cap_cache_line, "ORIENTATION=", 12) == 0) {
            if (num_supported_orientations < MAX_VALUES)
                supported_orientations[num_supported_orientations++] =
                    atoi(mp_cap_cache_line + 12);
        } else if (strncmp(mp_cap_cache_line, "PRINTMODE=", 10) == 0) {
            store_value(supported_print_modes, &num_supported_print_modes,
                        mp_cap_cache_line + 10);
        } else if (strncmp(mp_cap_cache_line, "QUALITY=", 8) == 0) {
            if (num_supported_quality < MAX_QUALITIES) {
                mp_cache_copy(supported_quality[num_supported_quality],
                              sizeof(supported_quality[num_supported_quality]),
                              mp_cap_cache_line + 8);
                num_supported_quality++;
            }
        }
    }

    Close(fh);
    has_media_ready = num_media_tray_mappings > 0 ? TRUE : FALSE;
    return TRUE;
}

static BOOL load_capability_cache_for_current_endpoint(void) {
    char host[64];
    int port = -1;
    char env_cache[64];
    char envarc_cache[64];

    if (!parse_ip_and_port(ip_buffer, host, sizeof(host), &port))
        return FALSE;
    if (port <= 0) port = 80;

    unit_cache_path(current_unit_index, FALSE, env_cache, sizeof(env_cache));
    unit_cache_path(current_unit_index, TRUE, envarc_cache, sizeof(envarc_cache));

    if (mp_cache_endpoint_matches((CONST_STRPTR)env_cache, host, port, driver_path_buffer))
        return mp_cache_load_file((CONST_STRPTR)env_cache);

    if (mp_cache_endpoint_matches((CONST_STRPTR)envarc_cache, host, port, driver_path_buffer))
        return mp_cache_load_file((CONST_STRPTR)envarc_cache);

    return FALSE;
}

static void apply_cached_capabilities(struct Window *win) {
    if (!win) return;

    update_engine_dropdown(win);
    update_media_dropdown(win);
    update_print_mode_dropdown(win);
    update_scaling_dropdown(win);
    update_quality_dropdown(win);

    /* Put the user's saved Unit0 choices back on top of the available lists. */
    apply_job_defaults_to_gadgets(win);
}

/* Reloads everything for current_unit_index: saved Unit%d config, its
 * cached capabilities (or "Not Detected" ghosting if there is none yet),
 * and the print-mode radio state. Used both when switching the Unit
 * dropdown and by File > Reload Driver Settings. */
static void reload_current_unit(struct Window *win) {
    mp_cache_clear_capabilities();

    if (load_driver_config())
        custom_printf("MintPRINT Unit%d loaded\n", current_unit_index);
    else
        custom_printf("No Unit%d found; using MintPRINT defaults\n", current_unit_index);

    seed_saved_option_labels();
    load_print_mode();

    if (!win) return;

    apply_driver_config_to_gadgets(win);

    if (load_capability_cache_for_current_endpoint()) {
        apply_cached_capabilities(win);
        custom_printf("Loaded cached printer capabilities\n");
    } else {
        apply_saved_option_state(win);
    }

    apply_job_defaults_to_gadgets(win);

    {
        struct Gadget *print_mode_gadget = find_gadget_by_id(GAD_PRINT_MODE);
        if (print_mode_gadget) {
            GT_SetGadgetAttrs(print_mode_gadget, win, NULL,
                              GTCY_Active, print_mode,
                              TAG_DONE);
            RefreshGList(print_mode_gadget, win, NULL, 1);
        }
    }

    GT_RefreshWindow(win, NULL);
}



// Redirect printf to buffer
/* Draws the status box border and whatever lines output_buffer/output_line
 * currently hold. This is the box's ENTIRE on-screen paint, and it is only
 * ever a side effect of custom_printf() being called - the window is
 * WA_SimpleRefresh, so nothing repaints this non-gadget area automatically.
 * That includes IDCMP_REFRESHWINDOW: GT_BeginRefresh/GT_EndRefresh there
 * only repaints GadTools gadgets, never this hand-drawn area, so without
 * this being called from that handler too, anything that forces a refresh
 * (another window opening on top and closing again, dragging this window
 * partly offscreen, etc.) leaves the box LOOKING empty - output_buffer's
 * data is untouched throughout, only the paint was lost. */
static void redraw_output_box(void) {
    struct RastPort *rp;
    int line_height, output_area_top, output_area_bottom, start_line, i;

    if (!window) return;

    rp = window->RPort;
    if (font) SetFont(rp, font);
    SetAPen(rp, 1); // Text color
    SetBPen(rp, 0); // Background color
    SetDrMd(rp, JAM2);

    // Calculate the output area dimensions
    line_height = font->tf_YSize + 2;
    output_area_top = OUTPUT_TOP;
    output_area_bottom = output_area_top + (MAX_OUTPUT_LINES * line_height) - 1;

    // Draw the border
    SetAPen(rp, 1); // Border color
    RectFill(rp, OUTPUT_LEFT - 2, output_area_top - 2, OUTPUT_RIGHT + 2, output_area_top - 1); // Top
    RectFill(rp, OUTPUT_LEFT - 2, output_area_bottom + 1, OUTPUT_RIGHT + 2, output_area_bottom + 2); // Bottom
    RectFill(rp, OUTPUT_LEFT - 2, output_area_top - 2, OUTPUT_LEFT - 1, output_area_bottom + 2); // Left
    RectFill(rp, OUTPUT_RIGHT + 1, output_area_top - 2, OUTPUT_RIGHT + 2, output_area_bottom + 2); // Right

    // Clear the output area
    SetAPen(rp, 0); // Background color
    RectFill(rp, OUTPUT_LEFT, output_area_top, OUTPUT_RIGHT, output_area_bottom);

    // Draw the most recent lines (scrolling effect)
    start_line = (output_line > MAX_OUTPUT_LINES) ? (output_line - MAX_OUTPUT_LINES) : 0;
    for (i = 0; i < MAX_OUTPUT_LINES && (start_line + i) < output_line; i++) {
        int y = output_area_top + (i * line_height) + font->tf_Baseline;
        Move(rp, OUTPUT_LEFT, y);
        SetAPen(rp, 1); // Text color
        Text(rp, output_buffer[start_line + i], strlen(output_buffer[start_line + i]));
    }
}

void custom_printf(const char *format, ...) {
    // Special case: clear the output area if the format string is "CLEAR"
    if (strcmp(format, "CLEAR") == 0) {
        output_line = 0;
        redraw_output_box();
        return;
    }

    va_list args;
    va_start(args, format);

    // Dynamically allocate temp buffer
    char *temp = malloc(256);
    if (!temp) {
        va_end(args);
        return; // Fail silently if allocation fails
    }

    vsnprintf(temp, 256, format, args);
    va_end(args);

    // Strip trailing newline
    size_t len = strlen(temp);
    if (len > 0 && temp[len - 1] == '\n') {
        temp[len - 1] = '\0';
        len--;
    }

    // Shift buffer if full
    if (output_line >= MAX_OUTPUT_LINES) {
        for (int i = 0; i < MAX_OUTPUT_LINES - 1; i++) {
            strncpy(output_buffer[i], output_buffer[i + 1], MAX_OUTPUT_LINE_LENGTH);
        }
        output_line = MAX_OUTPUT_LINES - 1;
    }

    // Store new line
    strncpy(output_buffer[output_line], temp, MAX_OUTPUT_LINE_LENGTH - 1);
    output_buffer[output_line][MAX_OUTPUT_LINE_LENGTH - 1] = '\0';
    output_line++;

    // Free the temp buffer
    free(temp);

    redraw_output_box();
}

int load_ilbm_to_rgb(const char *filename, unsigned char **rgb_out, int *width_out, int *height_out) {
    struct jpeg_data data;
    memset(&data, 0, sizeof(data));
    printf("Attempting to load IFF: %s\n", filename);

    if (load_iff_direct(filename, &data) != 0) {
        printf("load_iff_direct failed\n");
        return -1;
    }

    int num_pixels = data.width * data.height;
    printf("Loaded: %d x %d padded = %d px\n", data.width, data.height, num_pixels);

    *rgb_out = AllocVec(num_pixels * 3, MEMF_ANY);
    if (!*rgb_out) {
        printf("AllocVec failed\n");
        free_jpeg_data(&data);
        return -1;
    }

    for (int i = 0; i < num_pixels; i++) {
        (*rgb_out)[i * 3 + 0] = data.red[i];
        (*rgb_out)[i * 3 + 1] = data.green[i];
        (*rgb_out)[i * 3 + 2] = data.blue[i];
    }

    *width_out = data.width;
    *height_out = data.height;
    free_jpeg_data(&data);
    return 0;
}

// Creates a valid PWG header and uncompressed RGB data
int rgb_to_pwg_memory(unsigned char *rgb_data, int width, int height, unsigned char **pwg_out, int *pwg_size_out) {
    int row_bytes = width * 3;
    int header_size = 1796; // Standard PWG header size
    int data_size = row_bytes * height;
    int total_size = header_size + data_size;

    unsigned char *buffer = malloc(total_size);
    if (!buffer) return -1;
    memset(buffer, 0, total_size);

    // PWG header - 1796 bytes total
    // See Apple Raster Format spec for details
    buffer[0] = 'R'; buffer[1] = 'a'; buffer[2] = 'S'; buffer[3] = '2'; // PWG magic
    buffer[4] = 0x00; buffer[5] = 0x00; buffer[6] = 0x00; buffer[7] = 0x02; // Version 2
    buffer[8] = 0x00; buffer[9] = 0x00; buffer[10] = 0x00; buffer[11] = 0x01; // Number of pages = 1

    // Page 1 - raster attributes
    *(int *)&buffer[20] = width;       // pixelsPerLine
    *(int *)&buffer[24] = height;      // linesPerPage
    *(int *)&buffer[28] = 8;           // bitsPerColor
    *(int *)&buffer[32] = 24;          // bitsPerPixel
    *(int *)&buffer[36] = 1;           // color order (chunky)
    *(int *)&buffer[40] = 1;           // color space = sRGB
    *(int *)&buffer[44] = 0;           // compression = none

    // Set resolution (300 dpi)
    *(int *)&buffer[60] = 300;         // crossFeedTransform (dpi)
    *(int *)&buffer[64] = 300;         // feedTransform (dpi)

    // Copy raw RGB data after header
    memcpy(buffer + header_size, rgb_data, data_size);

    *pwg_out = buffer;
    *pwg_size_out = total_size;
    return 0;
}

// Existing functions (unchanged)
int rgb_to_pwg(const char *filename, unsigned char *rgb_data, int width, int height) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("Failed to open PWG file: %s\n", filename);
        return -1;
    }

    char header[128] = {0};
    memcpy(header, "RaS2", 4);
    header[4] = 0x00;
    header[8] = 0x00;
    header[12] = 0x00;
    header[16] = 0x00;
    header[20] = 0x00;
    header[24] = (width >> 24) & 0xFF;
    header[25] = (width >> 16) & 0xFF;
    header[26] = (width >> 8) & 0xFF;
    header[27] = width & 0xFF;
    header[28] = (height >> 24) & 0xFF;
    header[29] = (height >> 16) & 0xFF;
    header[30] = (height >> 8) & 0xFF;
    header[31] = height & 0xFF;
    header[32] = 8;
    header[36] = 3;
    header[40] = 3;
    header[44] = (width * 3 >> 24) & 0xFF;
    header[45] = (width * 3 >> 16) & 0xFF;
    header[46] = (width * 3 >> 8) & 0xFF;
    header[47] = (width * 3) & 0xFF;
    fwrite(header, 1, 128, file);

    fwrite(rgb_data, 1, width * height * 3, file);

    fclose(file);
    return 0;
}
// Wrapper to convert RGB to PWG
int convert_to_pwg(unsigned char *rgb, int w, int h, unsigned char **pwg_out, int *pwg_size_out) {
    char pwgfile[256];
    snprintf(pwgfile, sizeof(pwgfile), "UHD:temp.pwg");

    if (rgb_to_pwg(pwgfile, rgb, w, h) != 0) {
        return -1;
    }

    FILE *fp = fopen(pwgfile, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    rewind(fp);

    *pwg_out = malloc(size);
    if (!*pwg_out) {
        fclose(fp);
        return -1;
    }

    fread(*pwg_out, 1, size, fp);
    fclose(fp);
    *pwg_size_out = size;
    return 0;
}
/* ---------------------------------------------------------------------
 * DEVS:Printers/MintPRINT install helper
 *
 * MintPrint Settings ships next to the compiled MintPRINT printer.device
 * driver (PROGDIR:MintPRINT). If the driver is not yet installed in
 * DEVS:Printers/, offer to copy it in and point the user at Printer Prefs.
 * ------------------------------------------------------------------- */
#define MINTPRINT_DRIVER_DEST ((CONST_STRPTR)"DEVS:Printers/MintPRINT")
#define MINTPRINT_DRIVER_SRC  ((CONST_STRPTR)"PROGDIR:MintPRINT")

static BOOL mp_file_exists(CONST_STRPTR name) {
    BPTR lock = Lock(name, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
}

static BOOL mp_copy_file(CONST_STRPTR src, CONST_STRPTR dst) {
    BPTR in, out;
    UBYTE *buf;
    LONG nread;
    BOOL ok = TRUE;

    in = Open(src, MODE_OLDFILE);
    if (!in) return FALSE;

    out = Open(dst, MODE_NEWFILE);
    if (!out) {
        Close(in);
        return FALSE;
    }

    buf = AllocVec(32768, MEMF_ANY);
    if (!buf) {
        Close(out);
        Close(in);
        return FALSE;
    }

    while ((nread = Read(in, buf, 32768)) > 0) {
        if (Write(out, buf, nread) != nread) {
            ok = FALSE;
            break;
        }
    }
    if (nread < 0) ok = FALSE;

    FreeVec(buf);
    Close(out);
    Close(in);

    if (!ok) DeleteFile(dst);
    return ok;
}

static void mp_launch_printer_prefs(void) {
    /* SYS_Asynch without explicit SYS_Input/SYS_Output shares the CALLER's
     * own console handles with the new process - and an async process
     * closes its input/output when it exits, which then closes the
     * caller's (this program's, and its launching Shell's) console out
     * from under it. Give the child its own private NIL: handles instead
     * so it owns and closes only those. */
    BPTR in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    BPTR out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);

    if (SystemTags((CONST_STRPTR)"SYS:Prefs/Printer", SYS_Asynch, TRUE,
                   SYS_Input, (ULONG)in, SYS_Output, (ULONG)out,
                   TAG_DONE) != 0) {
        printf("Could not launch SYS:Prefs/Printer automatically\n");
        printf("Please open Printer preferences manually.\n");
        if (in) Close(in);
        if (out) Close(out);
    }
}

/* Reads this project's own driver build-counter out of a driver file, by
 * scanning for the literal "MPDRVREV:<decimal>" marker embedded in
 * printertag.s (see there for why: the compiled driver FILE on disk is a
 * standard AmigaDOS hunk-format load module, not a raw blob starting at
 * its code entry point, so there is no reliable FIXED BYTE OFFSET to read
 * this from - a scannable marker is the same approach AmigaOS's own
 * "Version" command uses for "$VER:" strings). Returns FALSE if the file
 * can't be opened or the marker isn't found. */
#define MP_DRIVER_REV_MARKER "MPDRVREV:"
#define MP_DRIVER_REV_SCAN_MAX 65536

static BOOL mp_read_driver_revision(CONST_STRPTR path, UWORD *revision_out) {
    BPTR file;
    UBYTE *buf;
    LONG got;
    LONG marker_len = (LONG)strlen(MP_DRIVER_REV_MARKER);
    LONG i;
    BOOL found = FALSE;

    file = Open(path, MODE_OLDFILE);
    if (!file) return FALSE;

    buf = AllocVec(MP_DRIVER_REV_SCAN_MAX, MEMF_ANY);
    if (!buf) {
        Close(file);
        return FALSE;
    }

    got = Read(file, buf, MP_DRIVER_REV_SCAN_MAX);
    Close(file);

    if (got < marker_len) {
        FreeVec(buf);
        return FALSE;
    }

    for (i = 0; i <= got - marker_len; i++) {
        if (memcmp(buf + i, MP_DRIVER_REV_MARKER, marker_len) == 0) {
            LONG j = i + marker_len;
            ULONG value = 0;
            BOOL any_digit = FALSE;

            while (j < got && buf[j] >= '0' && buf[j] <= '9') {
                value = value * 10UL + (ULONG)(buf[j] - '0');
                any_digit = TRUE;
                j++;
            }
            if (any_digit) {
                *revision_out = (UWORD)value;
                found = TRUE;
            }
            break;
        }
    }

    FreeVec(buf);
    return found;
}

static void show_about(struct Window *win) {
    struct EasyStruct es;
    char msg[512];
    char installed_str[32];
    char bundled_str[32];
    UWORD installed_rev = 0, bundled_rev = 0;

    if (mp_read_driver_revision(MINTPRINT_DRIVER_DEST, &installed_rev)) {
        snprintf(installed_str, sizeof(installed_str), "rev %u", (unsigned)installed_rev);
    } else {
        strcpy(installed_str, "not installed / unknown");
    }
    if (mp_read_driver_revision(MINTPRINT_DRIVER_SRC, &bundled_rev)) {
        snprintf(bundled_str, sizeof(bundled_str), "rev %u", (unsigned)bundled_rev);
    } else {
        strcpy(bundled_str, "not found");
    }

    snprintf(msg, sizeof(msg),
        "MintPRINT v1.0.1 - IPP/AirPrint printing for AmigaOS\n\n"
        "Installed driver (DEVS:Printers/MintPRINT): %s\n"
        "Bundled driver (next to this program): %s\n\n"
        "Bug reports and source:\n"
        "github.com/boingball/MintPRINT\n\n"
        "If this saved you a trip to the printer shop:\n"
        "buymeacoffee.com/boingball",
        installed_str, bundled_str);

    es.es_StructSize = sizeof(struct EasyStruct);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"About MintPrint Settings";
    es.es_TextFormat = (UBYTE *)msg;
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(win, &es, NULL);
}

static void check_and_offer_driver_install(struct Window *win) {
    struct EasyStruct es;
    char msg[192];
    UWORD src_rev, dest_rev;
    BOOL have_src_rev, have_dest_rev;

    if (!mp_file_exists(MINTPRINT_DRIVER_SRC)) {
        printf("MintPRINT driver not found next to this program; skipping install check.\n");
        return;
    }

    es.es_StructSize = sizeof(struct EasyStruct);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"MintPrint Settings";

    if (mp_file_exists(MINTPRINT_DRIVER_DEST)) {
        /* Already installed - only bother the user if the copy bundled
         * next to this program is a newer build than what's installed. */
        have_src_rev = mp_read_driver_revision(MINTPRINT_DRIVER_SRC, &src_rev);
        have_dest_rev = mp_read_driver_revision(MINTPRINT_DRIVER_DEST, &dest_rev);

        if (!have_src_rev || !have_dest_rev || src_rev <= dest_rev) {
            return; /* up to date, or revision unreadable - leave it alone */
        }

        snprintf(msg, sizeof(msg),
                 "A newer MintPRINT driver is available\n(installed: rev %u, bundled: rev %u).\nUpdate DEVS:Printers/MintPRINT now?",
                 (unsigned)dest_rev, (unsigned)src_rev);
        es.es_TextFormat = (UBYTE *)msg;
        es.es_GadgetFormat = (UBYTE *)"Update|Later";

        if (!EasyRequest(win, &es, NULL)) return;

        if (mp_copy_file(MINTPRINT_DRIVER_SRC, MINTPRINT_DRIVER_DEST)) {
            printf("Updated MintPRINT driver to rev %u in DEVS:Printers/MintPRINT\n", (unsigned)src_rev);
            printf("Reboot (or otherwise unload the old driver segment) before printing.\n");

            es.es_TextFormat = (UBYTE *)"MintPRINT driver updated.\n\nReboot before printing - the old driver segment\nalready resident in memory will not pick up this\nfile until then.";
            es.es_GadgetFormat = (UBYTE *)"OK";
            EasyRequest(win, &es, NULL);
        } else {
            es.es_TextFormat = (UBYTE *)"Could not copy the driver to DEVS:Printers/.\nCheck disk space and write access.";
            es.es_GadgetFormat = (UBYTE *)"OK";
            EasyRequest(win, &es, NULL);
        }
        return;
    }

    es.es_TextFormat = (UBYTE *)"The MintPRINT printer driver is not installed in\nDEVS:Printers/. Install it now?";
    es.es_GadgetFormat = (UBYTE *)"Install|Cancel";

    if (EasyRequest(win, &es, NULL)) {
        if (mp_copy_file(MINTPRINT_DRIVER_SRC, MINTPRINT_DRIVER_DEST)) {
            printf("Installed MintPRINT driver to DEVS:Printers/MintPRINT\n");

            es.es_TextFormat = (UBYTE *)"MintPRINT driver installed.\n\nOpen Printer preferences now and select\n'MintPRINT' as your printer, then save.\n\nReboot before printing - a driver segment already\nresident in memory will not pick up this file until then.";
            es.es_GadgetFormat = (UBYTE *)"Open Printer Prefs|Later";
            if (EasyRequest(win, &es, NULL)) {
                mp_launch_printer_prefs();
            }
        } else {
            es.es_TextFormat = (UBYTE *)"Could not copy the driver to DEVS:Printers/.\nCheck disk space and write access.";
            es.es_GadgetFormat = (UBYTE *)"OK";
            EasyRequest(win, &es, NULL);
        }
    }
}

/* ---------------------------------------------------------------------
 * LAN printer discovery (SSDP)
 *
 * Sends a single SSDP M-SEARCH multicast and collects distinct source
 * addresses that reply within a few seconds. Any AirPrint/network printer
 * that answers UPnP discovery (most consumer inkjets/lasers do, alongside
 * mDNS) shows up here as a candidate; the actual IPP capability check
 * still goes through the same query_printer_attributes() used by the
 * Query button once the user picks one from the list.
 * ------------------------------------------------------------------- */
static void ssdp_extract_server(const char *buf, char *out, int out_size) {
    const char *p = strstr(buf, "SERVER:");
    if (!p) p = strstr(buf, "Server:");
    if (!p) p = strstr(buf, "server:");
    out[0] = '\0';
    if (p) {
        int i = 0;
        p += 7;
        while (*p == ' ') p++;
        while (*p && *p != '\r' && *p != '\n' && i < out_size - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
    }
}

static BOOL discovery_ip_seen(struct DiscoveredPrinter *results, int count, const char *ip) {
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(results[i].ip, ip) == 0) return TRUE;
    }
    return FALSE;
}

static int ssdp_discover_printers(struct DiscoveredPrinter *results, int max_results) {
    int sockfd;
    struct sockaddr_in dest;
    char msearch[256];
    char *buf;
    int count = 0;
    int poll_num;
    const int max_polls = 10; /* ~500ms per poll => ~5s total scan time */

    if (max_results <= 0) return 0;

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        printf("Discovery: could not create UDP socket\n");
        return 0;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr((STRPTR)"239.255.255.250");

    snprintf(msearch, sizeof(msearch),
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: ssdp:all\r\n"
        "\r\n");

    if (sendto(sockfd, msearch, strlen(msearch), 0,
               (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("Discovery: SSDP send failed (no route to 239.255.255.250?)\n");
        CloseSocket(sockfd);
        return 0;
    }

    buf = malloc(1024);
    if (!buf) {
        CloseSocket(sockfd);
        return 0;
    }

    for (poll_num = 0; poll_num < max_polls && count < max_results; poll_num++) {
        fd_set readfds;
        struct timeval tv;
        long ready;
        struct sockaddr_in from;
        socklen_t fromlen;
        ssize_t received;

        if (window) {
            struct IntuiMessage *imsg;
            while ((imsg = GT_GetIMsg(window->UserPort))) {
                GT_ReplyIMsg(imsg);
            }
        }

        /* Bound each poll to ~500ms with WaitSelect rather than trusting
         * SO_RCVTIMEO on a datagram socket (not every bsdsocket.library
         * stack honours it) or a non-blocking-mode ioctl (this NDK's name
         * for it, FNONBIO, turned out not to work either). WaitSelect is
         * the one bsdsocket.library primitive this is built directly on. */
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        ready = WaitSelect(sockfd + 1, &readfds, NULL, NULL, &tv, NULL);
        if (ready <= 0) {
            continue; /* timeout or error this poll; try again */
        }

        fromlen = sizeof(from);
        memset(&from, 0, sizeof(from));
        received = recvfrom(sockfd, buf, 1023, 0, (struct sockaddr *)&from, &fromlen);
        if (received <= 0) {
            continue;
        }
        buf[received] = '\0';

        {
            char ipstr[16];
            const unsigned char *addr_bytes = (const unsigned char *)&from.sin_addr;

            /* sin_addr is already in network (big-endian) byte order, so the
             * raw bytes are the dotted-decimal octets left to right. Formats
             * manually rather than via inet_ntoa(), which this NDK does not
             * declare for bsdsocket.library. */
            snprintf(ipstr, sizeof(ipstr), "%u.%u.%u.%u",
                     addr_bytes[0], addr_bytes[1], addr_bytes[2], addr_bytes[3]);

            /* Skip loopback replies (e.g. the host's own SSDP responder
             * echoing back through some emulated/NAT network setups) -
             * never a real network printer. */
            if (addr_bytes[0] == 127) {
                continue;
            }

            if (ipstr[0] && !discovery_ip_seen(results, count, ipstr)) {
                char server_info[64];
                ssdp_extract_server(buf, server_info, sizeof(server_info));

                strncpy(results[count].ip, ipstr, sizeof(results[count].ip) - 1);
                results[count].ip[sizeof(results[count].ip) - 1] = '\0';

                if (server_info[0]) {
                    snprintf(results[count].label, sizeof(results[count].label),
                             "%s (%s)", ipstr, server_info);
                } else {
                    snprintf(results[count].label, sizeof(results[count].label),
                             "%s", ipstr);
                }

                printf("Discovery: found %s\n", results[count].label);
                count++;
            }
        }
    }

    free(buf);
    CloseSocket(sockfd);
    return count;
}

/* ---------------------------------------------------------------------
 * LAN printer discovery (mDNS / Bonjour / AirPrint)
 *
 * Most current printers advertise IPP over mDNS-SD (_ipp._tcp.local),
 * not SSDP, so this is the discovery path that actually matters for
 * AirPrint-style printers. Builds a minimal DNS PTR query by hand (no
 * name compression in the query - only ever one question) with the "QU"
 * unicast-response bit set, so responders reply directly to our source
 * port instead of over multicast. That means this never needs to join
 * the 224.0.0.251 multicast group to receive replies, keeping it on the
 * same plain send/WaitSelect/recvfrom shape already proven for SSDP.
 *
 * Deliberately does not decode the DNS response payload (PTR/SRV/TXT
 * records, name-compression pointers, ...): that is real parsing work
 * with real edge cases, and getting it wrong risks the same kind of
 * lock-up/crash this file has already hit twice on this NDK. All that is
 * used from a reply is which address it came from - good enough to
 * populate the picker; the follow-up IPP query after selection is what
 * actually pulls in the printer's real details.
 * ------------------------------------------------------------------- */
static int build_mdns_ptr_query(unsigned char *buf, int buf_size) {
    static const unsigned char header[12] = {
        0x00, 0x00, /* ID - unused, mDNS clients don't need to match it */
        0x00, 0x00, /* Flags - standard query */
        0x00, 0x01, /* QDCOUNT = 1 */
        0x00, 0x00, /* ANCOUNT */
        0x00, 0x00, /* NSCOUNT */
        0x00, 0x00  /* ARCOUNT */
    };
    static const char *labels[] = { "_ipp", "_tcp", "local", NULL };
    int off;
    int i;

    if (buf_size < 33) return 0;

    memcpy(buf, header, sizeof(header));
    off = sizeof(header);

    for (i = 0; labels[i]; i++) {
        int len = (int)strlen(labels[i]);
        buf[off++] = (unsigned char)len;
        memcpy(buf + off, labels[i], len);
        off += len;
    }
    buf[off++] = 0x00; /* root label terminator */

    buf[off++] = 0x00; buf[off++] = 0x0C; /* QTYPE = PTR (12) */
    buf[off++] = 0x80; buf[off++] = 0x01; /* QCLASS = IN, QU bit set */

    return off;
}

/* Appends newly-found, distinct, non-loopback responders to results[],
 * starting at index *count_io, up to max_results. Returns the new count. */
static int mdns_discover_printers(struct DiscoveredPrinter *results, int count_io, int max_results) {
    int sockfd;
    struct sockaddr_in dest;
    unsigned char query[64];
    int query_len;
    char *buf;
    int count = count_io;
    int poll_num;
    const int max_polls = 10; /* ~500ms per poll => ~5s total scan time */

    if (count >= max_results) return count;

    query_len = build_mdns_ptr_query(query, sizeof(query));
    if (query_len <= 0) {
        printf("Discovery: could not build mDNS query\n");
        return count;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        printf("Discovery: could not create mDNS socket\n");
        return count;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(5353);
    dest.sin_addr.s_addr = inet_addr((STRPTR)"224.0.0.251");

    if (sendto(sockfd, (char *)query, query_len, 0,
               (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("Discovery: mDNS send failed (no route to 224.0.0.251?)\n");
        CloseSocket(sockfd);
        return count;
    }

    buf = malloc(1024);
    if (!buf) {
        CloseSocket(sockfd);
        return count;
    }

    for (poll_num = 0; poll_num < max_polls && count < max_results; poll_num++) {
        fd_set readfds;
        struct timeval tv;
        long ready;
        struct sockaddr_in from;
        socklen_t fromlen;
        ssize_t received;

        if (window) {
            struct IntuiMessage *imsg;
            while ((imsg = GT_GetIMsg(window->UserPort))) {
                GT_ReplyIMsg(imsg);
            }
        }

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        ready = WaitSelect(sockfd + 1, &readfds, NULL, NULL, &tv, NULL);
        if (ready <= 0) {
            continue;
        }

        fromlen = sizeof(from);
        memset(&from, 0, sizeof(from));
        received = recvfrom(sockfd, buf, 1023, 0, (struct sockaddr *)&from, &fromlen);
        /* A real DNS response needs at least a 12-byte header with a
         * non-zero answer count, and a unicast QU reply comes from the
         * responder's own port 5353. Cheap enough sanity checks to reject
         * unrelated UDP traffic without decoding the message itself. */
        if (received < 12 || from.sin_port != htons(5353)) {
            continue;
        }
        if (buf[6] == 0 && buf[7] == 0) {
            continue; /* ANCOUNT == 0: not actually answering anything */
        }

        {
            char ipstr[16];
            const unsigned char *addr_bytes = (const unsigned char *)&from.sin_addr;

            snprintf(ipstr, sizeof(ipstr), "%u.%u.%u.%u",
                     addr_bytes[0], addr_bytes[1], addr_bytes[2], addr_bytes[3]);

            if (addr_bytes[0] == 127) {
                continue;
            }

            if (!discovery_ip_seen(results, count, ipstr)) {
                strncpy(results[count].ip, ipstr, sizeof(results[count].ip) - 1);
                results[count].ip[sizeof(results[count].ip) - 1] = '\0';
                snprintf(results[count].label, sizeof(results[count].label),
                         "%s (mDNS/IPP)", ipstr);

                printf("Discovery: found %s\n", results[count].label);
                count++;
            }
        }
    }

    free(buf);
    CloseSocket(sockfd);
    return count;
}

/* Runs both discovery mechanisms and merges the results: SSDP catches
 * printers/print servers that answer UPnP discovery, mDNS catches the
 * more common AirPrint/Bonjour-style IPP advertisement. */
static int discover_printers_on_lan(struct DiscoveredPrinter *results, int max_results) {
    int count;

    printf("Searching LAN for printers (SSDP)...\n");
    count = ssdp_discover_printers(results, max_results);

    printf("Searching LAN for printers (mDNS)...\n");
    count = mdns_discover_printers(results, count, max_results);

    return count;
}

/* Small GadTools dialog listing discovered candidates as a cycle gadget.
 * Mirrors the main window's CreateContext/CreateGadget/OpenWindowTags
 * pattern so it reuses the same, already-proven idioms. */
static BOOL run_discovery_selection(struct Window *parent,
                                     struct DiscoveredPrinter *results,
                                     int count,
                                     char *chosen_ip,
                                     int chosen_ip_size) {
    struct Screen *dscreen;
    APTR dvi;
    struct Gadget *dglist = NULL;
    struct Gadget *gad;
    struct NewGadget ng;
    struct Window *dwin;
    STRPTR *labels;
    BOOL picked = FALSE;
    BOOL terminated = FALSE;
    UWORD topborder;
    int i;

    (void)parent;

    if (count <= 0) return FALSE;

    dscreen = LockPubScreen(NULL);
    if (!dscreen) return FALSE;

    dvi = GetVisualInfo(dscreen, TAG_DONE);
    if (!dvi) {
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    labels = AllocVec((count + 1) * sizeof(STRPTR), MEMF_CLEAR);
    if (!labels) {
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }
    for (i = 0; i < count; i++) {
        labels[i] = (STRPTR)results[i].label;
    }
    labels[count] = NULL;

    topborder = dscreen->WBorTop + (dscreen->Font->ta_YSize + 1);

    gad = CreateContext(&dglist);
    if (!gad) {
        FreeVec(labels);
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    ng.ng_TextAttr = &Topaz80;
    ng.ng_VisualInfo = dvi;
    ng.ng_Flags = 0;
    ng.ng_LeftEdge = 10;
    ng.ng_TopEdge = 10 + topborder;
    ng.ng_Width = 410;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Found:";
    ng.ng_GadgetID = GAD_DISC_CYCLE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)labels,
        GTCY_Active, 0,
        TAG_DONE);
    if (!gad) {
        FreeGadgets(dglist);
        FreeVec(labels);
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    ng.ng_TopEdge += 26;
    ng.ng_LeftEdge = 10;
    ng.ng_Width = 120;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"_Use Selected";
    ng.ng_GadgetID = GAD_DISC_USE;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        FreeGadgets(dglist);
        FreeVec(labels);
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    ng.ng_LeftEdge = 290;
    ng.ng_Width = 120;
    ng.ng_GadgetText = (STRPTR)"_Cancel";
    ng.ng_GadgetID = GAD_DISC_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        FreeGadgets(dglist);
        FreeVec(labels);
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    dwin = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Select Discovered Printer",
        WA_Gadgets, (ULONG)dglist,
        WA_Width, 430,
        WA_InnerHeight, 70,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_CloseGadget, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | CYCLEIDCMP,
        WA_PubScreen, (ULONG)dscreen,
        TAG_DONE);

    if (!dwin) {
        FreeGadgets(dglist);
        FreeVec(labels);
        FreeVisualInfo(dvi);
        UnlockPubScreen(NULL, dscreen);
        return FALSE;
    }

    GT_RefreshWindow(dwin, NULL);

    while (!terminated) {
        struct IntuiMessage *imsg;

        Wait(1L << dwin->UserPort->mp_SigBit);
        imsg = GT_GetIMsg(dwin->UserPort);
        while (!terminated && imsg) {
            struct Gadget *g = (struct Gadget *)imsg->IAddress;
            ULONG cls = imsg->Class;
            GT_ReplyIMsg(imsg);

            if (cls == IDCMP_CLOSEWINDOW) {
                terminated = TRUE;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(dwin);
                GT_EndRefresh(dwin, TRUE);
            } else if (cls == IDCMP_GADGETUP) {
                if (g->GadgetID == GAD_DISC_CANCEL) {
                    terminated = TRUE;
                } else if (g->GadgetID == GAD_DISC_USE) {
                    struct Gadget *cyc = dglist;
                    ULONG selected = 0;
                    while (cyc && cyc->GadgetID != GAD_DISC_CYCLE) cyc = cyc->NextGadget;
                    if (cyc) {
                        GT_GetGadgetAttrs(cyc, dwin, NULL,
                                          GTCY_Active, (ULONG)&selected,
                                          TAG_DONE);
                    }
                    if (selected < (ULONG)count) {
                        strncpy(chosen_ip, results[selected].ip, chosen_ip_size - 1);
                        chosen_ip[chosen_ip_size - 1] = '\0';
                        picked = TRUE;
                    }
                    terminated = TRUE;
                }
            }
            imsg = GT_GetIMsg(dwin->UserPort);
        }
    }

    CloseWindow(dwin);
    FreeGadgets(dglist);
    FreeVec(labels);
    FreeVisualInfo(dvi);
    UnlockPubScreen(NULL, dscreen);

    return picked;
}

// Updated query_printer_attributes with fixed mapping logic and tray name parsing
int query_printer_attributes(const char *ip, int port, char *response, int maxlen) {
    custom_printf("CLEAR");
    if (operation_in_progress) {
        printf("Operation already in progress, please wait...\n");
        return -1;
    }
    operation_in_progress = TRUE;

    // Reset all supported values
    num_supported_formats = 0;
    num_supported_media = 0;
    num_supported_output_modes = 0;
    num_supported_sides = 0;
    num_supported_scaling = 0;
    num_supported_orientations = 0;
    num_supported_media_sources = 0;
    num_supported_print_modes = 0;
    num_supported_quality = 0;
    num_media_tray_mappings = 0;
    has_media_ready = FALSE;
    printer_make_model[0] = '\0';

    // Allocate buffers for parsing
    char *name = malloc(512);
    char *value = malloc(512);
    if (!name || !value) {
        printf("Memory allocation failed\n");
        if (name) free(name);
        if (value) free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    // Build IPP payload for Get-Printer-Attributes request
    unsigned char *ipp_payload = malloc(2048);// Dynamically allocate
    if (!ipp_payload) {
        printf("Failed to allocate memory for IPP payload\n");
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }
    int offset = 0;

    char uri[128];
    if (port == 80 || port == 631) {
        snprintf(uri, sizeof(uri), "ipp://%s/ipp/print", ip);
    } else {
        snprintf(uri, sizeof(uri), "ipp://%s:%d/ipp/print", ip, port);
    }
    int uri_len = strlen(uri);

    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01; // IPP 1.1
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B; // Get-Printer-Attributes
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01; // Request ID

    ipp_payload[offset++] = 0x01; // Operation attributes group
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    const char *requested = "media-source-supported,media-ready,printer-input-tray,printer-state,print-color-mode-supported,print-scaling-supported,print-quality-supported,document-format-supported,printer-make-and-model";
    int requested_len = strlen(requested);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "requested-attributes", 18); offset += 18;
    ipp_payload[offset++] = (requested_len >> 8) & 0xFF;
    ipp_payload[offset++] = requested_len & 0xFF;
    memcpy(&ipp_payload[offset], requested, requested_len); offset += requested_len;
    //Scaling
    int scaling_len = strlen(selected_scaling);
    ipp_payload[offset++] = 0x44; // keyword
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0d;
    memcpy(&ipp_payload[offset], "print-scaling", 13); offset += 13;
    ipp_payload[offset++] = (scaling_len >> 8) & 0xFF;
    ipp_payload[offset++] = scaling_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_scaling, scaling_len); offset += scaling_len;
    ipp_payload[offset++] = 0x03; // End of attributes

    // Build HTTP header
    char *http_header = malloc(256); // Dynamically allocate
    if (!http_header) {
        printf("Failed to allocate memory for HTTP header\n");
        free(ipp_payload);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }
    snprintf(http_header, 256,
             "POST /ipp HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             ip, offset);

    // Open socket
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        snprintf(response, maxlen, "Socket creation failed");
        free(http_header);
        free(ipp_payload);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }
    
    // Set a very short timeout to minimize blocking
    struct timeval timeout = {5, 0}; // 5-second timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        CloseSocket(sockfd);
        free(http_header);
        free(ipp_payload);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        snprintf(response, maxlen, "Invalid IP address: %s", ip);
        CloseSocket(sockfd);
        free(http_header);
        free(ipp_payload);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    printf("Connecting to printer...\n");
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        if (errno != EINPROGRESS) {
            snprintf(response, maxlen, "Failed to connect to printer");
            CloseSocket(sockfd);
            free(http_header);
            free(ipp_payload);
            free(name);
            free(value);
            operation_in_progress = FALSE;
            return -1;
        }
    }

    // Process GUI events
    if (window) {
        struct IntuiMessage *imsg;
        while ((imsg = GT_GetIMsg(window->UserPort))) {
            GT_ReplyIMsg(imsg);
        }
    }

    // Send request
    printf("Sending request...\n");
    if (send(sockfd, http_header, strlen(http_header), 0) < 0 ||
        send(sockfd, (char *)ipp_payload, offset, 0) < 0) {
        snprintf(response, maxlen, "Failed to send request");
        CloseSocket(sockfd);
        free(http_header);
        free(ipp_payload);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    free(http_header);
    free(ipp_payload);

    // Process GUI events
    if (window) {
        struct IntuiMessage *imsg;
        while ((imsg = GT_GetIMsg(window->UserPort))) {
            GT_ReplyIMsg(imsg);
        }
    }

    // Receive response in chunks to avoid blocking. Keeps reading past the
    // header separator until the declared Content-Length worth of body has
    // actually arrived (or the printer closes the connection, which is how
    // completion is confirmed when no Content-Length was given) - a response
    // that merely contains "\r\n\r\n" is not necessarily a *complete* one,
    // and treating it as complete produced an intermittent (network-timing
    // dependent) bug where a short first recv() was silently accepted and
    // parsed as if the printer had reported no capabilities at all.
    printf("Waiting for response...\n");
    int total_received = 0;
    int max_attempts = 40; // 40 attempts at 100ms each = 4 seconds max
    int attempt = 0;
    int header_start = 0; // advanced past any interim "1xx" response below
    int body_off = -1;
    int content_len = -1; // -1 = not yet known

    while (total_received < maxlen - 1 && attempt < max_attempts) {
        ssize_t received = recv(sockfd, response + total_received, maxlen - 1 - total_received, 0);
        if (received > 0) {
            total_received += received;
            response[total_received] = '\0';

            if (body_off < 0) {
                int off = mp_http_find_body(response, total_received, header_start);
                if (off >= 0) {
                    int status = mp_http_status(response, total_received, header_start);
                    if (status >= 100 && status < 200) {
                        // Interim response (e.g. "100 Continue") - skip it
                        // and keep waiting for the response that actually
                        // carries the IPP payload.
                        printf("Skipping interim HTTP %d response\n", status);
                        header_start = off;
                    } else {
                        char *cp;
                        body_off = off;
                        cp = strstr(response + header_start, "Content-Length:");
                        if (cp) {
                            sscanf(cp, "Content-Length: %d", &content_len);
                            printf("Content-Length: %d\n", content_len);
                        } else {
                            printf("No Content-Length header found\n");
                        }
                    }
                }
            }

            if (body_off >= 0 && content_len >= 0 &&
                (total_received - body_off) >= content_len) {
                break; // Got the full header and the declared IPP payload
            }
        } else if (received == 0) {
            break; // Connection closed - the printer is done sending
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                snprintf(response, maxlen, "Receive error");
                CloseSocket(sockfd);
                free(name);
                free(value);
                operation_in_progress = FALSE;
                return -1;
            }
        }

        // Process GUI events to keep the mouse responsive
        if (window) {
            struct IntuiMessage *imsg;
            while ((imsg = GT_GetIMsg(window->UserPort))) {
                GT_ReplyIMsg(imsg);
            }
        }

        // Wait a bit before retrying
        Delay(5); // 100ms delay (50 ticks per second on Amiga)
        attempt++;
    }

    if (total_received == 0) {
        snprintf(response, maxlen, "No response or timeout");
        CloseSocket(sockfd);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    response[total_received] = '\0';

    // Find the start of the IPP payload (past any interim response already
    // skipped above)
    if (body_off < 0) body_off = mp_http_find_body(response, total_received, header_start);
    if (body_off < 0) {
        printf("Failed to find IPP payload (no \\r\\n\\r\\n separator)\n");
        CloseSocket(sockfd);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }
    char *ipp_start = response + body_off;
    int ipp_len = total_received - body_off;

    // Don't parse a response we know is short: if the printer told us how
    // many body bytes to expect and we didn't get that many (timed out or
    // the connection dropped mid-response), that's a failed scan, not a
    // printer that reported empty capabilities.
    if (content_len >= 0 && ipp_len < content_len) {
        printf("Incomplete IPP response: got %d of %d declared bytes\n", ipp_len, content_len);
        CloseSocket(sockfd);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    if (ipp_len < 8) {
        printf("IPP response too short: %d bytes\n", ipp_len);
        CloseSocket(sockfd);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }

    // Check the IPP header
    printf("IPP Version: 0x%02x%02x\n", (unsigned char)ipp_start[0], (unsigned char)ipp_start[1]);
    printf("IPP Status: 0x%02x%02x\n", (unsigned char)ipp_start[2], (unsigned char)ipp_start[3]);
    printf("Request ID: 0x%02x%02x%02x%02x\n", (unsigned char)ipp_start[4], (unsigned char)ipp_start[5], (unsigned char)ipp_start[6], (unsigned char)ipp_start[7]);

    int pos = 8; // Skip header
    int attributes_processed = 0;
    int max_attributes = 1000; // Safety limit to prevent infinite loops
    char current_name[512] = ""; // Store the current attribute name for multi-value attributes

    while (pos < ipp_len && attributes_processed < max_attributes) {
        unsigned char tag = ipp_start[pos++];
        if (tag == 0x03) {
            break; // End of attributes
        }

        if (tag >= 0x01 && tag <= 0x05) { // Attribute group
            int group_start_pos = pos;
            while (pos < ipp_len && ipp_start[pos] > 0x05) {
                int attr_start_pos = pos;
                unsigned char value_tag = ipp_start[pos++];

                if (pos + 2 > ipp_len) {
                    pos = ipp_len; // Force exit
                    break;
                }
                int name_len = ((unsigned char)ipp_start[pos] << 8) | (unsigned char)ipp_start[pos + 1]; pos += 2;

                if (name_len == 0) {
                    strncpy(name, current_name, 512);
                    name[511] = '\0';
                } else {
                    if (name_len < 0 || name_len >= 512 || pos + name_len > ipp_len) {
                        pos = ipp_len; // Force exit
                        break;
                    }
                    strncpy(name, ipp_start + pos, name_len); name[name_len] = '\0'; pos += name_len;
                    strncpy(current_name, name, 512);
                    current_name[511] = '\0';
                }

                if (pos + 2 > ipp_len) {
                    pos = ipp_len; // Force exit
                    break;
                }
                int value_len = ((unsigned char)ipp_start[pos] << 8) | (unsigned char)ipp_start[pos + 1]; pos += 2;
                if (value_len < 0 || value_len >= 512 || pos + value_len > ipp_len) {
                    pos = ipp_len; // Force exit
                    break;
                }

                if (value_tag == 0x34 || value_tag == 0x37) {
                    pos += value_len;
                } else {
                    strncpy(value, ipp_start + pos, value_len); value[value_len] = '\0'; pos += value_len;

                    if (strcmp(name, "media-source-supported") == 0 && value_tag == 0x44) {
                        store_value(supported_media_sources, &num_supported_media_sources, value);
                    } else if (strcmp(name, "media-ready") == 0 && value_tag == 0x44) {
                        has_media_ready = TRUE;
                        store_value(supported_media, &num_supported_media, value);
                        int found = 0;
                        for (int i = 0; i < num_media_tray_mappings; i++) {
                            if (strcmp(media_tray_map[i].source, "auto") == 0) {
                                found = 1;
                                break;
                            }
                        }
                        if (!found && num_media_tray_mappings < MAX_VALUES) {
                            strncpy(media_tray_map[num_media_tray_mappings].media, value, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].source, "auto", MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].trayName, "AUTO", MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].medianame, "Unknown", MAX_ATTR_LEN - 1);
                            num_media_tray_mappings++;
                        }
                    } else if (strcmp(name, "printer-input-tray") == 0 && value_tag == 0x30) {
                        char source[MAX_ATTR_LEN] = "";
                        char trayName[MAX_ATTR_LEN] = "";
                        char medianame[MAX_ATTR_LEN] = "Unknown";
                        char media[MAX_ATTR_LEN] = "";
                        int index = -1;

                        char value_copy[512];
                        strncpy(value_copy, value, sizeof(value_copy) - 1);
                        value_copy[sizeof(value_copy) - 1] = '\0';

                        char *token = strtok(value_copy, ";");
                        while (token) {
                            if (strncmp(token, "name=", 5) == 0) {
                                strncpy(trayName, token + 5, MAX_ATTR_LEN - 1);
                            } else if (strncmp(token, "tray-name=", 10) == 0) {
                                strncpy(trayName, token + 10, MAX_ATTR_LEN - 1);
                            } else if (strncmp(token, "medianame=", 10) == 0) {
                                strncpy(medianame, token + 10, MAX_ATTR_LEN - 1);
                            } else if (strncmp(token, "media=", 6) == 0) {
                                strncpy(media, token + 6, MAX_ATTR_LEN - 1);
                            } else if (strncmp(token, "index=", 6) == 0) {
                                index = atoi(token + 6);
                                if (index == 1) strncpy(source, "auto", MAX_ATTR_LEN - 1);
                                else if (index == 2) strncpy(source, "by-pass-tray", MAX_ATTR_LEN - 1);
                                else if (index == 3) strncpy(source, "tray-1", MAX_ATTR_LEN - 1);
                                else if (index == 4) strncpy(source, "tray-2", MAX_ATTR_LEN - 1);

                                if (trayName[0] == '\0') {
                                    strncpy(trayName, source, MAX_ATTR_LEN - 1);
                                }
                            }
                            token = strtok(NULL, ";");
                        }

                        int found = 0;
                        for (int i = 0; i < num_media_tray_mappings; i++) {
                            if (strcmp(media_tray_map[i].source, source) == 0) {
                                strncpy(media_tray_map[i].trayName, trayName, MAX_ATTR_LEN - 1);
                                strncpy(media_tray_map[i].medianame, medianame, MAX_ATTR_LEN - 1);
                                found = 1;
                                break;
                            }
                        }
                        if (!found && num_media_tray_mappings < MAX_VALUES && media[0] != '\0') {
                            strncpy(media_tray_map[num_media_tray_mappings].media, media, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].source, source, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].trayName, trayName, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].medianame, medianame, MAX_ATTR_LEN - 1);
                            num_media_tray_mappings++;
                        }
                    } else if (strcmp(name, "printer-state") == 0 && value_tag == 0x21 && value_len == 4) {
                        int state = (ipp_start[pos - value_len] << 24) |
                                    (ipp_start[pos - value_len + 1] << 16) |
                                    (ipp_start[pos - value_len + 2] << 8) |
                                    (ipp_start[pos - value_len + 3]);
                        printf("Printer state: %d\n", state);
                    } else if (strcmp(name, "print-color-mode-supported") == 0 && value_tag == 0x44) {
                        store_value(supported_print_modes, &num_supported_print_modes, value);
                        printf("Added print-color-mode-supported: %s\n", value); }
                    else if (strcmp(name, "print-scaling-supported") == 0 && value_tag == 0x44) {
                        store_value(supported_scaling, &num_supported_scaling, value);
                        printf("Added print-scaling-supported: %s\n", value);
                    } else if (strcmp(name, "print-quality-supported") == 0 && value_tag == 0x21) {
                        int quality = atoi(value);
                        if (num_supported_quality < MAX_QUALITIES) {
                            switch (quality) {
                                case 3: strcpy(supported_quality[num_supported_quality], "draft"); break;
                                case 4: strcpy(supported_quality[num_supported_quality], "normal"); break;
                                case 5: strcpy(supported_quality[num_supported_quality], "high"); break;
                                default: sprintf(supported_quality[num_supported_quality], "q%d", quality); break;
                            }
                            num_supported_quality++;
                        }
                    } else if (strcmp(name, "document-format-supported") == 0 && value_tag == 0x49) {
                        store_value(supported_formats, &num_supported_formats, value);
                    } else if (strcmp(name, "printer-make-and-model") == 0 &&
                               (value_tag == 0x41 || value_tag == 0x42)) {
                        strncpy(printer_make_model, value, sizeof(printer_make_model) - 1);
                        printer_make_model[sizeof(printer_make_model) - 1] = '\0';
                    }
                }

                if (pos == attr_start_pos) {
                    pos = ipp_len; // Force exit
                    break;
                }

                if (num_supported_print_modes > 0) {
                    // Ensure selected_print_mode is still valid
                    int match_found = 0;
                    for (int i = 0; i < num_supported_print_modes; i++) {
                        if (strcmp(supported_print_modes[i], selected_print_mode) == 0) {
                            print_mode = i;
                            match_found = 1;
                            break;
                        }
                    }
                    if (!match_found) {
                        strcpy(selected_print_mode, supported_print_modes[0]);
                        print_mode = 0;
                        printf("No match for saved print mode, defaulting to: %s\n", selected_print_mode);
                    }
                }

                attributes_processed++;
                if (window) {
                    struct IntuiMessage *imsg;
                    while ((imsg = GT_GetIMsg(window->UserPort))) {
                        GT_ReplyIMsg(imsg);
                    }
                }
            }

            if (pos == group_start_pos) {
                pos++;
            }
        } else {
            continue;
        }
    }

    if (attributes_processed >= max_attributes) {
        printf("Reached maximum attribute limit (%d), aborting parsing\n", max_attributes);
    }

    int media_index = 0;
    for (int i = 0; i < num_supported_media_sources; i++) {
        if (strcmp(supported_media_sources[i], "auto") == 0) continue;
        if (media_index >= num_supported_media) break;
        int found = 0;
        for (int j = 0; j < num_media_tray_mappings; j++) {
            if (strcmp(media_tray_map[j].source, supported_media_sources[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && num_media_tray_mappings < MAX_VALUES) {
            strncpy(media_tray_map[num_media_tray_mappings].media, supported_media[media_index], MAX_ATTR_LEN - 1);
            strncpy(media_tray_map[num_media_tray_mappings].source, supported_media_sources[i], MAX_ATTR_LEN - 1);
            strncpy(media_tray_map[num_media_tray_mappings].trayName, supported_media_sources[i], MAX_ATTR_LEN - 1);
            strncpy(media_tray_map[num_media_tray_mappings].medianame, "Unknown", MAX_ATTR_LEN - 1);
            num_media_tray_mappings++;
        }
        media_index++;
    }

    printf("Media-Tray Mappings:\n");
    for (int i = 0; i < num_media_tray_mappings; i++) {
        printf("- %s -> %s (%s), medianame=%s\n", media_tray_map[i].media, media_tray_map[i].source, media_tray_map[i].trayName, media_tray_map[i].medianame);
    }
    printf("Supported Sources:\n");
    for (int i = 0; i < num_supported_media_sources; i++) {
        printf("- %s\n", supported_media_sources[i]);
    }
    printf("Supported Print Modes:\n");
    for (int i = 0; i < num_supported_print_modes; i++) {
        printf("- %s\n", supported_print_modes[i]);
    }

    // Check if the current print mode is supported
    const char *current_mode = print_mode == 0 ? "monochrome" : "color";
    int mode_supported = 0;
    for (int i = 0; i < num_supported_print_modes; i++) {
        if (strcmp(supported_print_modes[i], current_mode) == 0) {
            mode_supported = 1;
            break;
        }
    }
    if (!mode_supported) {
        printf("Warning: Selected print mode '%s' is not supported by the printer.\n", current_mode);
    }

    // Cleanup
    CloseSocket(sockfd);
    free(name);
    free(value);
    operation_in_progress = FALSE;

    if (window && vi) update_media_dropdown(window);
    if (window) update_print_mode_dropdown(window);
    if (window) update_scaling_dropdown(window);
    ensure_quality_defaults();
    if (window) update_quality_dropdown(window);
    if (window) update_engine_dropdown(window);

    if (printer_make_model[0]) {
        printf("Printer: %s\n", printer_make_model);
    } else {
        printf("Printer did not report printer-make-and-model\n");
    }

    if (window) {
        struct Gadget *model_gadget = find_gadget_by_id(GAD_MODEL_DISPLAY);
        if (model_gadget) {
            GT_SetGadgetAttrs(model_gadget, window, NULL,
                              GTST_String, (ULONG)printer_make_model,
                              TAG_DONE);
        }
        /* Preview the freshly-queried (not yet saved) model in the Unit
         * dropdown's current entry, rather than waiting for Save. */
        refresh_unit_dropdown(window);
    }

    if (num_supported_formats > 0) {
        printf("Printer document formats (%d):\n", num_supported_formats);
        for (int i = 0; i < num_supported_formats; i++) {
            printf("- %s\n", supported_formats[i]);
        }
    } else {
        printf("Printer did not report document-format-supported\n");
    }

    printf("query_printer_attributes completed\n");
    return 0;
}

/* Shared by the Query button and the post-discovery "Use Selected" path:
 * tries the given/default port then 631, and on success applies the fetched
 * capabilities to the gadgets exactly like a manual Query click. */
static void perform_query_flow(struct Window *win, const char *ip_only, int port_hint, char *response) {
    int chosen_port = (port_hint > 0) ? port_hint : 80;
    int ports_to_try[] = { chosen_port, 631 };
    int i, attempt;
    BOOL ok = FALSE;

    // A scan can fail an individual attempt for purely transient network
    // reasons (a slow/incomplete response - see query_printer_attributes).
    // Retry a few times per port before moving on, rather than treating one
    // flaky attempt as "the printer has no capabilities".
    for (i = 0; i < 2 && !ok; i++) {
        for (attempt = 0; attempt < 3 && !ok; attempt++) {
            printf("Trying %s:%d (attempt %d/3)...\n", ip_only, ports_to_try[i], attempt + 1);
            if (query_printer_attributes(ip_only, ports_to_try[i], response, MAX_BUFFER) == 0) {
                struct Gadget *ip_gadget;

                snprintf(ip_buffer, sizeof(ip_buffer), "%s:%d", ip_only, ports_to_try[i]);

                ip_gadget = glist;
                while (ip_gadget && ip_gadget->GadgetID != GAD_IP_STRING) {
                    ip_gadget = ip_gadget->NextGadget;
                }
                if (ip_gadget) {
                    GT_SetGadgetAttrs(ip_gadget, win, NULL,
                                      GTST_String, (ULONG)ip_buffer,
                                      TAG_DONE);
                }

                if (save_capability_cache(ip_only, ports_to_try[i], driver_path_buffer))
                    printf("Printer capabilities cached\n");
                else
                    printf("Warning: could not save printer capability cache\n");

                apply_job_defaults_to_gadgets(win);
                mp_check_any_engine_supported(win);
                ok = TRUE;
            } else {
                printf("Query attempt %d/3 on %s:%d failed\n", attempt + 1, ip_only, ports_to_try[i]);
            }
        }
    }

    if (!ok) {
        custom_printf("CLEAR");
        custom_printf("Scan failed - please try Query again");
    }
}

int send_pwg_print_job(const char *ip, int port, const char *media, const char *print_mode, unsigned char *pwg_data, int pwg_size) {
    if (operation_in_progress) {
        printf("Operation already in progress, please wait...\n");
        return -1;
    }
    operation_in_progress = TRUE;

    const char *selected_source = "auto";
    for (int i = 0; i < num_media_tray_mappings; i++) {
        if (strcmp(media_tray_map[i].media, media) == 0) {
            selected_source = media_tray_map[i].source;
            break;
        }
    }
    printf("Selected media: %s, source: %s, print mode: %s\n", media, selected_source, print_mode);

    struct sockaddr_in serv_addr;
    int sockfd = -1;
    unsigned char *ipp_payload = NULL;
    int offset = 0;
    char *http_header = NULL;
    char *response_buffer = NULL;
    int result = -1;

    ipp_payload = malloc(2048);
    if (!ipp_payload) {
        printf("Failed to allocate memory for IPP payload\n");
        operation_in_progress = FALSE;
        return -1;
    }
    memset(ipp_payload, 0, 2048);

    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp", ip);
    int uri_len = strlen(uri);

    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01;

    ipp_payload[offset++] = 0x01;
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    const char *job_name = "Amiga";
    int job_name_len = strlen(job_name);
    ipp_payload[offset++] = 0x42; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x08;
    memcpy(&ipp_payload[offset], "job-name", 8); offset += 8;
    ipp_payload[offset++] = (job_name_len >> 8) & 0xFF;
    ipp_payload[offset++] = job_name_len & 0xFF;
    memcpy(&ipp_payload[offset], job_name, job_name_len); offset += job_name_len;

    const char *doc_format = "image/pwg-raster";
    int doc_format_len = strlen(doc_format);
    ipp_payload[offset++] = 0x49; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0e;
    memcpy(&ipp_payload[offset], "document-format", 14); offset += 14;
    ipp_payload[offset++] = (doc_format_len >> 8) & 0xFF;
    ipp_payload[offset++] = doc_format_len & 0xFF;
    memcpy(&ipp_payload[offset], doc_format, doc_format_len); offset += doc_format_len;

    ipp_payload[offset++] = 0x02;

    int media_len = strlen(media);
    ipp_payload[offset++] = 0x44;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "media", 5); offset += 5;
    ipp_payload[offset++] = (media_len >> 8) & 0xFF;
    ipp_payload[offset++] = media_len & 0xFF;
    memcpy(&ipp_payload[offset], media, media_len); offset += media_len;

    int source_len = strlen(selected_source);
    ipp_payload[offset++] = 0x44;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0c;
    memcpy(&ipp_payload[offset], "media-source", 12); offset += 12;
    ipp_payload[offset++] = (source_len >> 8) & 0xFF;
    ipp_payload[offset++] = source_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_source, source_len); offset += source_len;


    if (!print_mode || strlen(print_mode) == 0) {
        print_mode = "monochrome";
    }
    int print_mode_len = strlen(print_mode);
    ipp_payload[offset++] = 0x44;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0f;
    memcpy(&ipp_payload[offset], "print-color-mode", 17); offset += 17;
    ipp_payload[offset++] = (print_mode_len >> 8) & 0xFF;
    ipp_payload[offset++] = print_mode_len & 0xFF;
    memcpy(&ipp_payload[offset], print_mode, print_mode_len); offset += print_mode_len;

    int scaling_len = strlen(selected_scaling);
    ipp_payload[offset++] = 0x44;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0d;
    memcpy(&ipp_payload[offset], "print-scaling", 13); offset += 13;
    ipp_payload[offset++] = (scaling_len >> 8) & 0xFF;
    ipp_payload[offset++] = scaling_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_scaling, scaling_len); offset += scaling_len;

    int quality_value = 4;
    if (strcmp(selected_quality, "draft") == 0) quality_value = 3;
    else if (strcmp(selected_quality, "high") == 0) quality_value = 5;

    ipp_payload[offset++] = 0x21;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0d;
    memcpy(&ipp_payload[offset], "print-quality", 13); offset += 13;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x04;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = quality_value;

    ipp_payload[offset++] = 0x21; // enum
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "printer-resolution", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x06;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01; // cross feed units = dpi
    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x2c; // 300
    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x2c; // 300
    ipp_payload[offset++] = 0x03;

    http_header = malloc(256);
    if (!http_header) {
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }
    snprintf(http_header, 256,
        "POST /ipp HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        ip, offset + pwg_size);

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        free(http_header);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }
    printf("Sent HTTP header\n");
    struct timeval timeout = {10, 0};
    struct timeval send_timeout = {10, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&send_timeout, sizeof(send_timeout));
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        CloseSocket(sockfd);
        free(http_header);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }
    printf("Sent IPP payload (%d bytes)\n", offset);
    printf("T. contlen: %d \n (header: %d\n, pwg: %d)\n", offset + pwg_size, offset, pwg_size);
    printf("Sending PWG data (%d bytes)...\n", pwg_size);
    if (send(sockfd, http_header, strlen(http_header), 0) < 0 ||
        send(sockfd, (char *)ipp_payload, offset, 0) < 0 ||
        safe_send(sockfd, (char *)pwg_data, pwg_size) < 0) {
        CloseSocket(sockfd);
        free(http_header);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    free(http_header);
    free(ipp_payload);

    response_buffer = malloc(4096);
    if (!response_buffer) {
        CloseSocket(sockfd);
        operation_in_progress = FALSE;
        return -1;
    }

    {
        int total_received = 0;
        int header_start = 0;
        int body_off = -1;
        int attempt;

        for (attempt = 0; attempt < 10; attempt++) {
            ssize_t received = recv(sockfd, response_buffer + total_received,
                                    4096 - 1 - total_received, 0);
            if (received <= 0) break;
            total_received += (int)received;
            response_buffer[total_received] = '\0';
            body_off = mp_http_find_body(response_buffer, total_received, header_start);
            if (body_off < 0) continue;
            {
                int status = mp_http_status(response_buffer, total_received, header_start);
                if (status >= 100 && status < 200) {
                    header_start = body_off;
                    body_off = -1;
                    continue;
                }
            }
            break;
        }

        if (body_off >= 0) {
            char *ipp_start = response_buffer + body_off;
            printf("IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
        } else {
            printf("No response or receive timeout.\n");
        }
    }

    free(response_buffer);
    CloseSocket(sockfd);
    operation_in_progress = FALSE;
    return 0;
}

// Updated send_print_job to send the selected tray (media-source) and print mode
int send_print_job(const char *ip, int port, const char *filename, const char *media, const char *print_mode) {
    if (operation_in_progress) {
        printf("Operation already in progress, please wait...\n");
        return -1;
    }
    operation_in_progress = TRUE;

    // Find the selected media's tray
    const char *selected_source = "auto"; // Default fallback
    for (int i = 0; i < num_media_tray_mappings; i++) {
        if (strcmp(media_tray_map[i].media, media) == 0) {
            selected_source = media_tray_map[i].source;
            break;
        }
    }
    printf("Selected media: %s, source: %s, print mode: %s\n", media, selected_source, print_mode);

    struct sockaddr_in serv_addr;
    int sockfd = -1;
    unsigned char *ipp_payload = NULL;
    int offset = 0;
    unsigned char *file_data = NULL;
    FILE *file = NULL;
    char *http_header = NULL;
    char *response_buffer = NULL; // Dynamically allocate
    int result = -1;

    // Allocate IPP payload dynamically
    ipp_payload = malloc(2048);
    if (!ipp_payload) {
        printf("Failed to allocate memory for IPP payload\n");
        operation_in_progress = FALSE;
        return -1;
    }
    memset(ipp_payload, 0, 2048);

    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp", ip);
    int uri_len = strlen(uri);

    // Open the file
    file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open file: %s\n", filename);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer for file data
    file_data = malloc(file_size);

    if (file_size <= 0) {
        printf("Invalid or empty file\n");
        fclose(file);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    if (!file_data) {
        printf("Failed to allocate memory for file data\n");
        fclose(file);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    // Read the file
    fread(file_data, 1, file_size, file);
    fclose(file);
    file = NULL;

    // Build IPP payload
    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01; // IPP version 1.1
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02; // Print-Job operation
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01; // Request ID

    ipp_payload[offset++] = 0x01; // Operation attributes group
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    const char *job_name = "Amiga";
    int job_name_len = strlen(job_name);
    ipp_payload[offset++] = 0x42; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x08;
    memcpy(&ipp_payload[offset], "job-name", 8); offset += 8;
    ipp_payload[offset++] = (job_name_len >> 8) & 0xFF;
    ipp_payload[offset++] = job_name_len & 0xFF;
    memcpy(&ipp_payload[offset], job_name, job_name_len); offset += job_name_len;

    const char *doc_format = "image/jpeg";
    int doc_format_len = strlen(doc_format);
    ipp_payload[offset++] = 0x49; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0e;
    memcpy(&ipp_payload[offset], "document-format", 14); offset += 14;
    ipp_payload[offset++] = (doc_format_len >> 8) & 0xFF;
    ipp_payload[offset++] = doc_format_len & 0xFF;
    memcpy(&ipp_payload[offset], doc_format, doc_format_len); offset += doc_format_len;

    ipp_payload[offset++] = 0x02; // Job Template Attributes group

    int media_len = strlen(media);
    ipp_payload[offset++] = 0x44; // keyword
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "media", 5); offset += 5;
    ipp_payload[offset++] = (media_len >> 8) & 0xFF;
    ipp_payload[offset++] = media_len & 0xFF;
    memcpy(&ipp_payload[offset], media, media_len); offset += media_len;

    // Add media-source attribute
    int source_len = strlen(selected_source);
    ipp_payload[offset++] = 0x44; // keyword
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0c;
    memcpy(&ipp_payload[offset], "media-source", 12); offset += 12;
    ipp_payload[offset++] = (source_len >> 8) & 0xFF;
    ipp_payload[offset++] = source_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_source, source_len); offset += source_len;
/*
    if (!print_mode || strlen(print_mode) == 0) {
        printf("Invalid print mode (empty), falling back to 'monochrome'\n");
        print_mode = "monochrome";
    }
    // Add print-color-mode attribute
    int print_mode_len = strlen(print_mode);
    ipp_payload[offset++] = 0x44; // keyword
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0f;
    memcpy(&ipp_payload[offset], "print-color-mode", 17); offset += 17;
    ipp_payload[offset++] = (print_mode_len >> 8) & 0xFF;
    ipp_payload[offset++] = print_mode_len & 0xFF;
    memcpy(&ipp_payload[offset], print_mode, print_mode_len); offset += print_mode_len;

    //Scaling Options
    int scaling_len = strlen(selected_scaling);
    ipp_payload[offset++] = 0x44; // keyword (for print-scaling)
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0d;
    memcpy(&ipp_payload[offset], "print-scaling", 13); offset += 13;
    ipp_payload[offset++] = (scaling_len >> 8) & 0xFF;
    ipp_payload[offset++] = scaling_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_scaling, scaling_len); offset += scaling_len;

    // Quality Options
    int quality_value = 4; // default to normal
    if (strcmp(selected_quality, "draft") == 0) quality_value = 3;
    else if (strcmp(selected_quality, "high") == 0) quality_value = 5;

    ipp_payload[offset++] = 0x21; // enum
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0d;
    memcpy(&ipp_payload[offset], "print-quality", 13); offset += 13;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x04;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = quality_value;
*/
    ipp_payload[offset++] = 0x03; // End of attributes

    http_header = malloc(256);
    if (!http_header) {
        printf("Failed to allocate memory for HTTP header\n");
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }
    snprintf(http_header, 256,
        "POST /ipp HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        ip, offset + file_size);

    printf("Sending JPEG to printer at %s...\n", ip);
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("Socket creation failed.\n");
        free(http_header);
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    struct timeval timeout = {10, 0}; // 10-second timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        CloseSocket(sockfd);
        free(http_header);
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address: %s\n", ip);
        CloseSocket(sockfd);
        free(http_header);
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect to printer.\n");
        CloseSocket(sockfd);
        free(http_header);
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    // Process GUI events
    if (window) {
        struct IntuiMessage *imsg;
        while ((imsg = GT_GetIMsg(window->UserPort))) {
            GT_ReplyIMsg(imsg);
        }
    }

    // Send the data
    if (send(sockfd, http_header, strlen(http_header), 0) < 0 ||
        send(sockfd, (char *)ipp_payload, offset, 0) < 0 ||
        send(sockfd, file_data, file_size, 0) < 0) {
        printf("Failed sending data.\n");
        CloseSocket(sockfd);
        free(http_header);
        free(file_data);
        free(ipp_payload);
        operation_in_progress = FALSE;
        return -1;
    }

    free(http_header);
    free(file_data);
    free(ipp_payload);

    printf("Waiting for response...\n");
    // Dynamically allocate response_buffer
    response_buffer = malloc(4096);
    if (!response_buffer) {
        printf("Failed to allocate memory for response buffer\n");
        CloseSocket(sockfd);
        operation_in_progress = FALSE;
        return -1;
    }

    {
        int total_received = 0;
        int header_start = 0;
        int body_off = -1;
        int attempt;

        for (attempt = 0; attempt < 10; attempt++) {
            ssize_t received = recv(sockfd, response_buffer + total_received,
                                    4096 - 1 - total_received, 0);
            if (received <= 0) break;
            total_received += (int)received;
            response_buffer[total_received] = '\0';
            body_off = mp_http_find_body(response_buffer, total_received, header_start);
            if (body_off < 0) continue;
            {
                int status = mp_http_status(response_buffer, total_received, header_start);
                if (status >= 100 && status < 200) {
                    header_start = body_off;
                    body_off = -1;
                    continue;
                }
            }
            break;
        }

        if (total_received == 0) {
            printf("No response or receive timeout.\n");
            CloseSocket(sockfd);
            free(response_buffer);
            operation_in_progress = FALSE;
            return -1;
        }

        if (body_off >= 0) {
            char *ipp_start = response_buffer + body_off;
            printf("IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
        } else {
            printf("Could not find IPP response payload.\n");
        }
    }

    free(response_buffer);
    CloseSocket(sockfd);
    operation_in_progress = FALSE;
    printf("send_print_job completed successfully\n");
    return 0;
}

// Function to create all GadTools gadgets
struct Gadget *createAllGadgets(struct Gadget **glistptr, void *vi, UWORD topborder) {
    struct NewGadget ng;
    struct Gadget *gad;

    // Initialize the gadget list
    gad = CreateContext(glistptr);
    if (!gad) {
        printf("Failed to create gadget context\n");
        return NULL;
    }

    // Set up the NewGadget structure
    ng.ng_TextAttr = &Topaz80;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = NG_HIGHLABEL;

    // Unit selector - which saved printer profile (ENV:MintPRINT/UnitN) is
    // being viewed/edited. Only Unit0 is what the driver actually prints
    // with; switching here reloads the rest of the form from that unit's
    // saved file.
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge = 5 + topborder;
    ng.ng_Width = 260;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Unit:";
    ng.ng_GadgetID = GAD_UNIT_DROPDOWN;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)unit_dropdown_labels,
        GTCY_Active, (ULONG)current_unit_index,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create unit dropdown\n");
        return NULL;
    }

    // Copies the selected unit's saved settings over Unit0, the only slot
    // the driver actually reads at print time - the practical way to
    // "switch which printer is active" without touching driver code.
    ng.ng_LeftEdge = 400;
    ng.ng_Width = 90;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"_Activate";
    ng.ng_GadgetID = GAD_SET_ACTIVE_BUTTON;
    ng.ng_Flags = 0;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create activate button\n");
        return NULL;
    }
    ng.ng_Flags = NG_HIGHLABEL;

    // IP string gadget
    ng.ng_LeftEdge = 165;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 190;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"_Printer IP/Host:";
    ng.ng_GadgetID = GAD_IP_STRING;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)ip_buffer,
        GTST_MaxChars, sizeof(ip_buffer) - 1,
        GA_Immediate, TRUE,
        GT_Underscore, '_',
        GACT_RELVERIFY, TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create IP string gadget\n");
        return NULL;
    }



    // Query button - kept beside the printer address field.
    ng.ng_LeftEdge = 400;
    ng.ng_Width = 90;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"_Query";
    ng.ng_GadgetID = GAD_QUERY_BUTTON;
    ng.ng_Flags = 0;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create query button\n");
        return NULL;
    }

    // Printer Model (read-only display) - shows printer-make-and-model
    // from the last successful Query for this unit. Not user-editable;
    // persisted via MODEL= in the unit's own config file on Save.
    ng.ng_LeftEdge = 165;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 290;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Printer Model:";
    ng.ng_GadgetID = GAD_MODEL_DISPLAY;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)printer_make_model,
        GTST_MaxChars, sizeof(printer_make_model) - 1,
        GA_Disabled, TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create model display\n");
        return NULL;
    }

    // Driver IPP path
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 200;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"IPP _Path:";
    ng.ng_GadgetID = GAD_IPP_PATH;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)driver_path_buffer,
        GTST_MaxChars, sizeof(driver_path_buffer) - 1,
        GA_Immediate, TRUE,
        GT_Underscore, '_',
        GACT_RELVERIFY, TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create IPP path gadget\n");
        return NULL;
    }

    // Discover button - sits below Query, searches the LAN for printers.
    // Nudged a few px below the shared IPP Path row so its bevel has
    // clear space from Query's above it, then restored so it doesn't
    // shift every row below.
    {
        UWORD row2_top = ng.ng_TopEdge;
        ng.ng_LeftEdge = 400;
        ng.ng_TopEdge = row2_top + 4;
        ng.ng_Width = 90;
        ng.ng_Height = 14;
        ng.ng_GadgetText = (STRPTR)"_Discover";
        ng.ng_GadgetID = GAD_DISCOVER_BUTTON;
        ng.ng_Flags = 0;
        gad = CreateGadget(BUTTON_KIND, gad, &ng,
            GT_Underscore, '_',
            TAG_DONE);
        if (!gad) {
            printf("Failed to create discover button\n");
            return NULL;
        }
        ng.ng_TopEdge = row2_top;
    }

    // Printer document engine: JPEG, PWG Raster, or PDF.
    // LeftEdge is nudged right of the other rows' shared 130 - this is the
    // longest label at this column ("Printer Engine:", 15 chars) and at 130
    // it renders with no left margin at all, clipping against the window
    // edge.
    ng.ng_LeftEdge = 160;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 180;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Printer Engine:";
    ng.ng_GadgetID = GAD_ENGINE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)engine_labels,
        GTCY_Active, mp_engine_active_index(),
        TAG_DONE);
    if (!gad) {
        printf("Failed to create printer engine gadget\n");
        return NULL;
    }

    // Keep/delete the diagnostic JPEG after a successful driver print
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 180;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Debug JPEG:";
    ng.ng_GadgetID = GAD_KEEPJOB;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)keep_job_labels,
        GTCY_Active, driver_keep_job ? 1 : 0,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create debug JPEG gadget\n");
        return NULL;
    }

    // Media dropdown
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 280;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Media (Tray):";
    ng.ng_GadgetID = GAD_MEDIA_DROPDOWN;
    ng.ng_Flags = NG_HIGHLABEL;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)media_dropdown_items,
        GTCY_Active, 0,
        GA_Disabled, driver_media_buffer[0] ? FALSE : TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create media dropdown\n");
        return NULL;
    }
    media_dropdown = gad;  // Save it globally

    // Scaling dropdown
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 150;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Scaling:";
    ng.ng_GadgetID = GAD_SCALING_MODE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
    GTCY_Labels, (ULONG)scaling_mode_labels,
    GTCY_Active, 0,
        GA_Disabled, driver_scaling_buffer[0] ? FALSE : TRUE,
    TAG_DONE);

    // Quality dropdown
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 150;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Quality:";
    ng.ng_GadgetID = GAD_QUALITY_MODE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)quality_mode_labels,
        GTCY_Active, 0,
        GA_Disabled, driver_quality_buffer[0] ? FALSE : TRUE,
        TAG_DONE);

    // Print Mode radio buttons
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 150;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Print Mode:";
    ng.ng_GadgetID = GAD_PRINT_MODE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)print_mode_labels,
        GTCY_Active, 0,
        GA_Disabled, driver_color_buffer[0] ? FALSE : TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create print mode radio buttons\n");
        return NULL;
    }

    // Test Print button
    ng.ng_LeftEdge = 10;
    ng.ng_TopEdge += 30;
    ng.ng_Width = 110;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"_Test Print";
    ng.ng_GadgetID = GAD_PRINT_BUTTON;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create print button\n");
        return NULL;
    }


    // Save button - same action as File -> Save Driver Settings.
    ng.ng_LeftEdge = 250;
    ng.ng_Width = 90;
    ng.ng_GadgetText = (STRPTR)"_Save";
    ng.ng_GadgetID = GAD_SAVE_BUTTON;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create save button\n");
        return NULL;
    }

    // Exit button
    ng.ng_LeftEdge = 350;
    ng.ng_Width = 90;
    ng.ng_GadgetText = (STRPTR)"_Exit";
    ng.ng_GadgetID = GAD_EXIT_BUTTON;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create exit button\n");
        return NULL;
    }

    return gad;
}

// Function to process window events using GadTools message handling
void process_window_events(struct Window *win) {
    struct IntuiMessage *imsg;
    ULONG imsgClass;
    UWORD imsgCode;
    struct Gadget *gad;
    BOOL terminated = FALSE;
    char ip_only[64];
    int port = -1;
    char *response = malloc(MAX_BUFFER); // Dynamically allocate
    if (!response) {
        printf("Failed to allocate memory for response buffer\n");
        return;
    }

    while (!terminated) {
        Wait(1L << win->UserPort->mp_SigBit);

        imsg = GT_GetIMsg(win->UserPort);
        while (!terminated && imsg) {
            gad = (struct Gadget *)imsg->IAddress;
            imsgClass = imsg->Class;
            imsgCode = imsg->Code;

            GT_ReplyIMsg(imsg);

            switch (imsgClass) {
                case IDCMP_GADGETUP:
                    switch (gad->GadgetID) {
                        case GAD_UNIT_DROPDOWN:
                        {
                            ULONG selected = 0;
                            GT_GetGadgetAttrs(gad, win, NULL,
                                              GTCY_Active, (ULONG)&selected,
                                              TAG_DONE);
                            if (selected < (ULONG)MAX_UNITS && (int)selected != current_unit_index) {
                                current_unit_index = (int)selected;
                                custom_printf("CLEAR");
                                reload_current_unit(win);
                            }
                        }
                        break;

                        case GAD_SET_ACTIVE_BUTTON:
                        {
                            custom_printf("CLEAR");

                            if (current_unit_index == 0) {
                                custom_printf("Unit0 is already the active printer.\n");
                            } else if (!unit_file_exists(current_unit_index)) {
                                custom_printf("Unit%d has no saved settings yet - nothing to activate.\n",
                                              current_unit_index);
                            } else {
                                char src_env[64], src_envarc[64];
                                char dst_env[64], dst_envarc[64];
                                BOOL ok;

                                unit_config_path(current_unit_index, FALSE, src_env, sizeof(src_env));
                                unit_config_path(current_unit_index, TRUE, src_envarc, sizeof(src_envarc));
                                unit_config_path(0, FALSE, dst_env, sizeof(dst_env));
                                unit_config_path(0, TRUE, dst_envarc, sizeof(dst_envarc));

                                ok = ensure_config_dir((CONST_STRPTR)"ENV:MintPRINT") &&
                                     ensure_config_dir((CONST_STRPTR)"ENVARC:MintPRINT") &&
                                     mp_copy_file((CONST_STRPTR)src_env, (CONST_STRPTR)dst_env) &&
                                     mp_copy_file((CONST_STRPTR)src_envarc, (CONST_STRPTR)dst_envarc);

                                if (ok) {
                                    char src_cache_env[64], src_cache_envarc[64];
                                    char dst_cache_env[64], dst_cache_envarc[64];

                                    /* Best-effort: carry the cached capabilities over too, so
                                     * Unit0 doesn't need a fresh Query. Fine if there is none. */
                                    unit_cache_path(current_unit_index, FALSE, src_cache_env, sizeof(src_cache_env));
                                    unit_cache_path(current_unit_index, TRUE, src_cache_envarc, sizeof(src_cache_envarc));
                                    unit_cache_path(0, FALSE, dst_cache_env, sizeof(dst_cache_env));
                                    unit_cache_path(0, TRUE, dst_cache_envarc, sizeof(dst_cache_envarc));
                                    mp_copy_file((CONST_STRPTR)src_cache_env, (CONST_STRPTR)dst_cache_env);
                                    mp_copy_file((CONST_STRPTR)src_cache_envarc, (CONST_STRPTR)dst_cache_envarc);

                                    custom_printf("Unit%d copied to Unit0 - it is now the active printer.\n",
                                                  current_unit_index);
                                    current_unit_index = 0;
                                    reload_current_unit(win);
                                    refresh_unit_dropdown(win);
                                } else {
                                    custom_printf("Could not copy Unit%d to Unit0.\n", current_unit_index);
                                }
                            }
                        }
                        break;

                        case GAD_MEDIA_DROPDOWN:
                        {
                            ULONG selected = ~0UL;
                            GT_GetGadgetAttrs(media_dropdown, win, NULL,
                                              GTCY_Active, (ULONG)&selected,
                                              TAG_DONE);
                            if (selected < (ULONG)num_media_tray_mappings) {
                                printf("Selected index = %lu, value = %s\n",
                                       selected, media_tray_map[selected].media);
                            } else {
                                printf("Invalid selection index = %lu\n", selected);
                            }
                        }
                        break;

                        case GAD_IP_STRING:
                        {
                            char *current_ip = NULL;
                            GT_RefreshWindow(win, NULL);
                            GT_GetGadgetAttrs(gad, window, NULL,
                                GTST_String, (ULONG)&current_ip,
                                TAG_DONE);
                            printf("Got pointer: %p\n", current_ip);
                            if (current_ip) {
                                printf("Raw IP string from gadget: '%s'\n", current_ip);
                                strncpy(ip_buffer, current_ip, sizeof(ip_buffer) - 1);
                                ip_buffer[sizeof(ip_buffer) - 1] = '\0';
                                printf("IP buffer after update: '%s'\n", ip_buffer);
                            } else {
                                printf("Failed to retrieve IP string from gadget\n");
                            }
                        }
                        break;
                        case GAD_PRINT_MODE:
                        {
                            ULONG selected = ~0UL;
                            GT_GetGadgetAttrs(gad, win, NULL,
                                              GTCY_Active, (ULONG)&selected,
                                              TAG_DONE);
                            print_mode = selected;
                            if (selected < num_supported_print_modes) {
                                strncpy(selected_print_mode, supported_print_modes[selected], MAX_ATTR_LEN - 1);
                                selected_print_mode[MAX_ATTR_LEN - 1] = '\0';
                                printf("Print mode set to: %s\n", selected_print_mode);
                            }
                        }
                        break;

                        case GAD_SCALING_MODE:
                        {
                            ULONG selected = ~0UL;
                            GT_GetGadgetAttrs(gad, win, NULL,
                                            GTCY_Active, (ULONG)&selected,
                                            TAG_DONE);
                            if (selected < num_supported_scaling) {
                                strncpy(selected_scaling, supported_scaling[selected], MAX_ATTR_LEN - 1);
                                selected_scaling[MAX_ATTR_LEN - 1] = '\0';
                                printf("Scaling mode set to: %s\n", selected_scaling);
                            }
                        }
                        break;

                        case GAD_QUERY_BUTTON:
                        {
                            GT_RefreshWindow(win, NULL);
                        
                            // Get IP string from gadget
                            struct Gadget *ip_gadget = glist;
                            while (ip_gadget && ip_gadget->GadgetID != GAD_IP_STRING) {
                                ip_gadget = ip_gadget->NextGadget;
                            }
                        
                            if (ip_gadget) {
                                char *ip_string = NULL;
                                ULONG success = GT_GetGadgetAttrs(ip_gadget, win, NULL,
                                                                  GTST_String, (ULONG)&ip_string,
                                                                  TAG_DONE);
                                if (success && ip_string) {
                                    strncpy(ip_buffer, ip_string, sizeof(ip_buffer) - 1);
                                    ip_buffer[sizeof(ip_buffer) - 1] = '\0';
                                    printf("IP buffer updated to: '%s'\n", ip_buffer);
                                }
                            }
                        
                            // Parse IP and optional port
                            if (!parse_ip_and_port(ip_buffer, ip_only, sizeof(ip_only), &port)) {
                                snprintf(response, MAX_BUFFER, "Invalid IP format");
                                return;
                            }
                        
                            // Try default + fallback ports, apply capabilities on success
                            perform_query_flow(win, ip_only, port, response);
                        }
                        break;

                        case GAD_DISCOVER_BUTTON:
                        {
                            struct DiscoveredPrinter found[MAX_DISCOVERY_RESULTS];
                            int found_count;
                            char chosen_ip[16];

                            GT_RefreshWindow(win, NULL);
                            printf("CLEAR");

                            found_count = discover_printers_on_lan(found, MAX_DISCOVERY_RESULTS);

                            if (found_count <= 0) {
                                printf("No printers found via SSDP or mDNS.\n");
                                printf("Enter the printer IP manually and press Query.\n");
                            } else {
                                printf("Found %d candidate device(s).\n", found_count);
                                if (run_discovery_selection(win, found, found_count, chosen_ip, sizeof(chosen_ip))) {
                                    struct Gadget *disc_ip_gadget = glist;

                                    strncpy(ip_buffer, chosen_ip, sizeof(ip_buffer) - 1);
                                    ip_buffer[sizeof(ip_buffer) - 1] = '\0';

                                    while (disc_ip_gadget && disc_ip_gadget->GadgetID != GAD_IP_STRING) {
                                        disc_ip_gadget = disc_ip_gadget->NextGadget;
                                    }
                                    if (disc_ip_gadget) {
                                        GT_SetGadgetAttrs(disc_ip_gadget, win, NULL,
                                                          GTST_String, (ULONG)ip_buffer,
                                                          TAG_DONE);
                                    }

                                    perform_query_flow(win, chosen_ip, 0, response);
                                } else {
                                    printf("Discovery selection cancelled.\n");
                                }
                            }
                        }
                        break;

                        case GAD_PRINT_BUTTON:
                        {
                            GT_RefreshWindow(win, NULL);
                            mintprint_test_page(win);
                        }
                        break;

                        case GAD_SAVE_BUTTON:
                            if (save_driver_config(win))
                                printf("MintPRINT Unit%d saved to ENV: and ENVARC:\n", current_unit_index);
                            else
                                printf("Failed to save MintPRINT Unit%d settings\n", current_unit_index);
                            break;

                        case GAD_EXIT_BUTTON:
                            terminated = TRUE;
                            break;
                    }
                    break;

                case IDCMP_CLOSEWINDOW:
                    terminated = TRUE;
                    break;

                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(win);
                    GT_EndRefresh(win, TRUE);
                    /* GT_BeginRefresh/EndRefresh only repaints GadTools
                     * gadgets - the status box is hand-drawn and needs its
                     * own replay here, or it looks emptied out any time
                     * something forces a refresh (e.g. Printer Prefs
                     * opening on top of this window and closing again). */
                    redraw_output_box();
                    break;

                    case IDCMP_MENUPICK:
                    {
                        ULONG code = imsg->Code;
                        while (code != MENUNULL) {
                            UWORD menu_num = MENUNUM(code);
                            UWORD item_num = ITEMNUM(code);
                    
                            if (menu_num == 0) { // File menu
                                switch (item_num) {
                                    case 0: // Save Settings
                                        save_print_mode();
                                        if (save_driver_config(win))
                                            printf("MintPRINT Unit%d saved to ENV: and ENVARC:\n", current_unit_index);
                                        else
                                            printf("Failed to save MintPRINT Unit%d settings\n", current_unit_index);
                                        break;

                                    case 1: // Load Settings
                                        reload_current_unit(win);
                                        break;

                                    case 3: // About MintPRINT...
                                        show_about(win);
                                        break;

                                    case 5: // Quit
                                        terminated = TRUE;
                                        break;
                                }
                            }
                    
                            code = MENUNULL; // Only handling one menu item per event
                        }
                    }
                    break;
                    
                    
            }

            imsg = GT_GetIMsg(win->UserPort);
        }
    }

    free(response); // Free the dynamically allocated buffer
}

// Main function
int main(void) {
    UWORD topborder;

    // Open libraries with version checks
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase) {
        printf("Failed to open intuition.library\n");
        return 1;
    }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!GfxBase) {
        printf("Failed to open graphics.library\n");
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    GadToolsBase = OpenLibrary("gadtools.library", 39);
    if (!GadToolsBase) {
        printf("Requires V37 gadtools.library\n");
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    SocketBase = OpenLibrary("bsdsocket.library", 0);
    if (!SocketBase) {
        printf("Failed to open bsdsocket.library\n");
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    // Open Topaz font
    font = OpenFont(&Topaz60);
    if (!font) {
        printf("Failed to open Topaz 6 font\n");
        CloseLibrary(SocketBase);
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    // Lock the default public screen
    screen = LockPubScreen(NULL);
    if (!screen) {
        printf("Could not lock public screen\n");
        CloseFont(font);
        CloseLibrary(SocketBase);
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    // Get visual info
    vi = GetVisualInfo(screen, TAG_DONE);
    if (!vi) {
        printf("Failed to get visual info\n");
        UnlockPubScreen(NULL, screen);
        CloseFont(font);
        CloseLibrary(SocketBase);
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    // Calculate top border
    topborder = screen->WBorTop + (screen->Font->ta_YSize + 1);
    /* Cycle label pointers already target process-lifetime static storage.
     * seed_saved_option_labels() populated those arrays above. */
    // Load the same Unit0 profile used by DEVS:Printers/MintPRINT.
    load_driver_config();
    seed_saved_option_labels();

    // Load print mode from ENV:
    load_print_mode();

    // Seed the Unit dropdown's labels from whatever is saved on disk.
    refresh_unit_dropdown(NULL);

    // Create gadgets
    if (!createAllGadgets(&glist, vi, topborder)) {
        printf("Failed to create gadgets\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, screen);
        CloseFont(font);
        CloseLibrary(SocketBase);
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    // Open window
    window = OpenWindowTags(NULL,
        WA_Title, (ULONG)"MintPrint Settings",
        WA_Gadgets, (ULONG)glist,
        WA_AutoAdjust, TRUE,
        WA_Width, 520,
        WA_MinWidth, 520,
        WA_InnerHeight, 350,
        WA_MinHeight, 350,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_NewLookMenus, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP | CYCLEIDCMP| IDCMP_MENUPICK,
        WA_PubScreen, (ULONG)screen,
        TAG_DONE);

    if (!window) {
        printf("Failed to open window\n");
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, screen);
        CloseFont(font);
        CloseLibrary(SocketBase);
        CloseLibrary(GadToolsBase);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Draw the status box's empty border immediately, rather than leaving
     * it invisible until the first status line happens to draw it. */
    custom_printf("CLEAR");

    // Set the initial state of the print mode radio buttons
    struct Gadget *print_mode_gadget = glist;
    while (print_mode_gadget && print_mode_gadget->GadgetID != GAD_PRINT_MODE) {
        print_mode_gadget = print_mode_gadget->NextGadget;
    }
    if (print_mode_gadget) {
        GT_SetGadgetAttrs(print_mode_gadget, window, NULL,
                          GTCY_Active, print_mode,
                          TAG_DONE);
    }

    menu = CreateMenus(menu_template, TAG_DONE);
    if (menu) {
        LayoutMenus(menu, vi,
            GTMN_NewLookMenus, TRUE,           // Enable standard white/grey look
            GTMN_FrontPen, 1,                  // Text pen (usually black)
            GTNM_BackPen, 0,                   // Background pen (usually white)
            TAG_DONE);
        SetMenuStrip(window, menu);
    } else {
        printf("Failed to create menus\n");
    }

    // Refresh window
    GT_RefreshWindow(window, NULL);

    // Offer to install DEVS:Printers/MintPRINT if it is missing.
    check_and_offer_driver_install(window);

    if (load_capability_cache_for_current_endpoint()) {
        apply_cached_capabilities(window);
        printf("Loaded cached printer capabilities\n");
    } else {
        apply_saved_option_state(window);
    }



    // Process events
    process_window_events(window);

    // Save print mode before exiting
    save_print_mode();

    // Cleanup
    // Ensure operation_in_progress is reset
    operation_in_progress = FALSE;

    // Process any remaining messages in the window's UserPort
    if (window) {
        struct IntuiMessage *imsg;
        while ((imsg = GT_GetIMsg(window->UserPort))) {
            GT_ReplyIMsg(imsg);
        }
    }

    /* Correct GadTools teardown order: first detach menus and close the
     * window, then free gadgets, then release the label backing memory.
     */
    if (window && menu) {
        ClearMenuStrip(window);
    }

    if (window) {
        CloseWindow(window);
        window = NULL;
    }

    if (glist) {
        FreeGadgets(glist);
        glist = NULL;
    }

    /* Cycle gadget labels are static process-lifetime storage. */
    cleanup_dropdown_labels();

    if (menu) {
        FreeMenus(menu);
        menu = NULL;
    }

    /*
     * GadTools VisualInfo belongs to the screen it was obtained from.
     * It must be released while the public-screen lock is still held.
     *
     * The old order did UnlockPubScreen() first and only then called
     * FreeVisualInfo().  That leaves GadTools using screen-related state
     * after our guarantee that the Screen pointer is still valid has gone,
     * and is particularly unfriendly to the classic OS3.1 libraries.
     *
     * Correct lifetime:
     *   FreeGadgets / FreeMenus
     *   FreeVisualInfo
     *   UnlockPubScreen
     */
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }

    if (screen) {
        UnlockPubScreen(NULL, screen);
        screen = NULL;
    }

    // Close the font only after GadTools no longer has VisualInfo using it.
    if (font) {
        CloseFont(font);
        font = NULL;
    }

    // Close libraries in reverse order of opening
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    if (GadToolsBase) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }

    return 0;
}