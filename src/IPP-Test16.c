/* MintPRINT prefs #9: compact address row and status-box fit. */
/* MintPRINT prefs #8: capability cache and output-area layout polish. */
/* Amiga IPP Print-Job Prototype with GUI
   Sends a JPEG file to an IPP printer (AirPrint-compatible)
   Compile with: m68k-amigaos-gcc -g -o IPP-test11 ipp-test11.c -lamiga -lsocket -lm
 PATCH INCOMING: Adds IFF -> RGB -> PWG -> IPP printing support to IPP-test15 */


#include <proto/exec.h>
#include <ctype.h> // for tolower()
#include <proto/dos.h>
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

#define OUTPUT_TOP     270 // Below Test Print / Save / Exit row
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
// Stack cookie to request a minimum stack size of 362 KB (102,400 bytes)
static const char USED min_stack[] = "$STACK:362144";

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
STRPTR *scaling_mode_labels = NULL;
char selected_quality[16] = "auto"; // Default
char supported_quality[MAX_QUALITIES][16];
int num_supported_quality = 0;
STRPTR *quality_mode_labels = NULL;
static char initial_quality_value[32] = "Not Detected";
static STRPTR initial_quality_mode[] = { initial_quality_value, NULL };
// Media dropdown state
char *selected_media = NULL;
struct Gadget *media_dropdown = NULL;
STRPTR *media_dropdown_items = NULL;
BOOL has_media_ready = FALSE;
struct Menu *menu = NULL;
struct MediaTrayMap media_tray_map[MAX_VALUES];
int num_media_tray_mappings = 0;

// Radio button labels for print mode
STRPTR *print_mode_labels = NULL;

static char initial_media_value[160] = "Not Detected";
static STRPTR initial_media_labels[] = { initial_media_value, NULL };



static struct NewMenu menu_template[] = {
    { NM_TITLE, (STRPTR)"File", 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Save Driver Settings", 0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Reload Driver Settings", 0, 0, 0, 0 },
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
static STRPTR engine_labels[] = { "JPEG", "PWG Raster", NULL };
char driver_media_buffer[MAX_ATTR_LEN] = "";
char driver_source_buffer[MAX_ATTR_LEN] = "";
char driver_color_buffer[MAX_ATTR_LEN] = "";
char driver_quality_buffer[MAX_ATTR_LEN] = "";
char driver_scaling_buffer[MAX_ATTR_LEN] = "";
char driver_sides_buffer[MAX_ATTR_LEN] = "";
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
        strcpy(driver_engine_buffer, engine_active == 1 ? "pwg-raster" : "jpeg");
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

    FPuts(file, "# MintPRINT Unit0 - written by MintPRINT Preferences\n");
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
    Close(file);
    return TRUE;
}

static BOOL save_driver_config(struct Window *win) {
    BOOL env_ok;
    BOOL envarc_ok;

    capture_driver_settings(win);

    if (!ensure_config_dir((CONST_STRPTR)"ENV:MintPRINT")) {
        printf("Could not create/find ENV:MintPRINT\n");
        return FALSE;
    }
    if (!ensure_config_dir((CONST_STRPTR)"ENVARC:MintPRINT")) {
        printf("Could not create/find ENVARC:MintPRINT\n");
        return FALSE;
    }

    env_ok = write_driver_config_file((CONST_STRPTR)"ENV:MintPRINT/Unit0");
    envarc_ok = write_driver_config_file((CONST_STRPTR)"ENVARC:MintPRINT/Unit0");
    return env_ok && envarc_ok;
}

static BOOL load_driver_config(void) {
    BPTR file;
    char line[192];
    char host[64] = "192.168.0.51";
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

    file = Open((CONST_STRPTR)"ENV:MintPRINT/Unit0", MODE_OLDFILE);
    if (!file)
        file = Open((CONST_STRPTR)"ENVARC:MintPRINT/Unit0", MODE_OLDFILE);

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
}

static void apply_saved_option_state(struct Window *win) {
    struct Gadget *g;

    if (!win) return;
    seed_saved_option_labels();

    if (num_media_tray_mappings == 0) {
        g = find_gadget_by_id(GAD_MEDIA_DROPDOWN);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)initial_media_labels,
                              GTCY_Active, 0,
                              GA_Disabled, driver_media_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_scaling == 0) {
        g = find_gadget_by_id(GAD_SCALING_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)initial_scaling_mode,
                              GTCY_Active, 0,
                              GA_Disabled, driver_scaling_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_quality == 0) {
        g = find_gadget_by_id(GAD_QUALITY_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)initial_quality_mode,
                              GTCY_Active, 0,
                              GA_Disabled, driver_quality_buffer[0] ? FALSE : TRUE,
                              TAG_DONE);
    }

    if (num_supported_print_modes == 0) {
        g = find_gadget_by_id(GAD_PRINT_MODE);
        if (g)
            GT_SetGadgetAttrs(g, win, NULL,
                              GTCY_Labels, (ULONG)initial_print_mode,
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

    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(mp);
    FreeBitMap(bm);
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
                          GTCY_Active, strcmp(driver_engine_buffer, "pwg-raster") == 0 ? 1 : 0,
                          TAG_DONE);

    GT_RefreshWindow(win, NULL);
}

// Add after successful query to rebuild media dropdown (Updated to show media (tray))
void update_media_dropdown(struct Window *win) {
    printf("Updating media dropdown, num_mappings=%d\n", num_media_tray_mappings);

    // Free old items
    if (media_dropdown_items) {
        for (int i = 0; media_dropdown_items[i]; i++) {
            FreeVec(media_dropdown_items[i]);
            media_dropdown_items[i] = NULL;
        }
        FreeVec(media_dropdown_items);
        media_dropdown_items = NULL;
    }

    // Determine the number of items to allocate (at least 1 for "No Media Available")
    int num_items = (num_media_tray_mappings > 0) ? num_media_tray_mappings : 1;

    // Allocate new array
    media_dropdown_items = AllocVec((num_items + 1) * sizeof(STRPTR), MEMF_CLEAR);
    if (!media_dropdown_items) {
        printf("Failed to allocate media dropdown items\n");
        return;
    }

    if (num_media_tray_mappings == 0) {
        printf("No media-tray mappings found, adding default entry.\n");
        media_dropdown_items[0] = AllocVec(strlen("No Media Available") + 1, MEMF_CLEAR);
        if (!media_dropdown_items[0]) {
            printf("Failed to allocate memory for default dropdown item\n");
            FreeVec(media_dropdown_items);
            media_dropdown_items = NULL;
            return;
        }
        strcpy(media_dropdown_items[0], "No Media Available");
        media_dropdown_items[1] = NULL;
    } else {
        // Fill with "Media (Tray)" format
        int i;
        for (i = 0; i < num_media_tray_mappings; i++) {
            // Dynamically allocate entry buffer
            char *entry = AllocVec(MAX_ATTR_LEN + 20, MEMF_CLEAR);
            if (!entry) {
                printf("Failed to allocate memory for entry buffer at index %d\n", i);
                // Free all previously allocated strings and the array
                for (int j = 0; j < i; j++) {
                    FreeVec(media_dropdown_items[j]);
                    media_dropdown_items[j] = NULL;
                }
                FreeVec(media_dropdown_items);
                media_dropdown_items = NULL;
                return;
            }

            // Validate media and trayName to avoid crashes
            const char *media = media_tray_map[i].media ? media_tray_map[i].media : "Unknown";
            const char *trayName = media_tray_map[i].trayName ? media_tray_map[i].trayName : "Unknown";
            snprintf(entry, MAX_ATTR_LEN + 20, "%s (%s)", media, trayName);

            media_dropdown_items[i] = AllocVec(strlen(entry) + 1, MEMF_CLEAR);
            if (!media_dropdown_items[i]) {
                printf("Failed to allocate memory for dropdown item %d\n", i);
                FreeVec(entry);
                // Free all previously allocated strings and the array
                for (int j = 0; j < i; j++) {
                    FreeVec(media_dropdown_items[j]);
                    media_dropdown_items[j] = NULL;
                }
                FreeVec(media_dropdown_items);
                media_dropdown_items = NULL;
                return;
            }
            strcpy(media_dropdown_items[i], entry);
            printf("Dropdown item %d: %s\n", i, media_dropdown_items[i]);
            FreeVec(entry);
        }
        media_dropdown_items[num_media_tray_mappings] = NULL;
    }

    // Update gadget
    if (media_dropdown && win) {
        GT_SetGadgetAttrs(media_dropdown, win, NULL,
                          GTCY_Labels, (ULONG)media_dropdown_items,
                          GTCY_Active, 0,
                          GA_Disabled, FALSE,
                          TAG_DONE);
        RefreshGList(media_dropdown, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_scaling_dropdown(struct Window *win) {
    if (scaling_mode_labels && scaling_mode_labels != initial_scaling_mode) {
        for (int i = 0; scaling_mode_labels[i]; i++) {
            FreeVec(scaling_mode_labels[i]);
        }
        FreeVec(scaling_mode_labels);
        scaling_mode_labels = NULL;
    }

    scaling_mode_labels = AllocVec((num_supported_scaling + 1) * sizeof(STRPTR), MEMF_ANY);
    if (!scaling_mode_labels) {
        printf("Failed to allocate scaling_mode_labels\n");
        return;
    }

    for (int i = 0; i < num_supported_scaling; i++) {
        scaling_mode_labels[i] = AllocVec(strlen(supported_scaling[i]) + 1, MEMF_ANY);
        if (scaling_mode_labels[i]) {
            strcpy(scaling_mode_labels[i], supported_scaling[i]);
        }
    }
    scaling_mode_labels[num_supported_scaling] = NULL;

    // Find and update the gadget
    struct Gadget *g = glist;
    while (g && g->GadgetID != GAD_SCALING_MODE)
        g = g->NextGadget;

    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)scaling_mode_labels,
                          GTCY_Active, 0,
                          GA_Disabled, FALSE,
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_print_mode_dropdown(struct Window *win) {
    // Free old items
    if (print_mode_labels && print_mode_labels != initial_print_mode) {
        for (int i = 0; print_mode_labels[i]; i++) {
            FreeVec(print_mode_labels[i]);
        }
        FreeVec(print_mode_labels);
        print_mode_labels = NULL;
        print_mode_labels = initial_print_mode;
    }

    print_mode_labels = AllocVec((num_supported_print_modes + 1) * sizeof(STRPTR), MEMF_ANY);
    if (!print_mode_labels) {
        printf("Failed to allocate memory for print_mode_labels\n");
        return;
    }

    for (int i = 0; i < num_supported_print_modes; i++) {
        print_mode_labels[i] = AllocVec(strlen(supported_print_modes[i]) + 1, MEMF_ANY);
        if (print_mode_labels[i]) {
            strcpy(print_mode_labels[i], supported_print_modes[i]);
        }
    }
    print_mode_labels[num_supported_print_modes] = NULL;

    // Update gadget if already created
    struct Gadget *g = glist;
    while (g && g->GadgetID != GAD_PRINT_MODE)
        g = g->NextGadget;
    
    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)(print_mode_labels ? print_mode_labels : initial_print_mode),
                          GTCY_Active, 0,
                          GA_Disabled, FALSE,
                          TAG_DONE);

        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void update_quality_dropdown(struct Window *win) {
    if (quality_mode_labels && quality_mode_labels != initial_quality_mode) {
        for (int i = 0; quality_mode_labels[i]; i++) {
            FreeVec(quality_mode_labels[i]);
        }
        FreeVec(quality_mode_labels);
        quality_mode_labels = NULL;
    }

    quality_mode_labels = AllocVec((num_supported_quality + 1) * sizeof(STRPTR), MEMF_ANY);
    if (!quality_mode_labels) return;

    for (int i = 0; i < num_supported_quality; i++) {
        quality_mode_labels[i] = AllocVec(strlen(supported_quality[i]) + 1, MEMF_ANY);
        if (quality_mode_labels[i]) {
            strcpy(quality_mode_labels[i], supported_quality[i]);
        }
    }
    quality_mode_labels[num_supported_quality] = NULL;

    struct Gadget *g = glist;
    while (g && g->GadgetID != GAD_QUALITY_MODE)
        g = g->NextGadget;

    if (g && win) {
        GT_SetGadgetAttrs(g, win, NULL,
                          GTCY_Labels, (ULONG)quality_mode_labels,
                          GTCY_Active, 0,
                          GA_Disabled, FALSE,
                          TAG_DONE);
        RefreshGList(g, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

void cleanup_dropdown_labels() {
    // Print Mode Labels
    if (print_mode_labels && print_mode_labels != initial_print_mode) {
        for (int i = 0; print_mode_labels[i]; i++) {
            if (print_mode_labels[i]) {
                FreeVec(print_mode_labels[i]);
                print_mode_labels[i] = NULL;
            }
        }
        FreeVec(print_mode_labels);
        print_mode_labels = NULL;
    }

    // Scaling Mode Labels
    if (scaling_mode_labels && scaling_mode_labels != initial_scaling_mode) {
        for (int i = 0; scaling_mode_labels[i]; i++) {
            if (scaling_mode_labels[i]) {
                FreeVec(scaling_mode_labels[i]);
                scaling_mode_labels[i] = NULL;
            }
        }
        FreeVec(scaling_mode_labels);
        scaling_mode_labels = NULL;
    }

    // Quality Mode Labels
    if (quality_mode_labels && quality_mode_labels != initial_quality_mode) {
        for (int i = 0; quality_mode_labels[i]; i++) {
            if (quality_mode_labels[i]) {
                FreeVec(quality_mode_labels[i]);
                quality_mode_labels[i] = NULL;
            }
        }
        FreeVec(quality_mode_labels);
        quality_mode_labels = NULL;
    }
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

    if (!host || !host[0] || port <= 0 || port > 65535 ||
        !path || path[0] != '/') {
        return FALSE;
    }

    if (!ensure_config_dir((CONST_STRPTR)"ENV:MintPRINT"))
        return FALSE;
    if (!ensure_config_dir((CONST_STRPTR)"ENVARC:MintPRINT"))
        return FALSE;

    env_ok = mp_cache_write_file((CONST_STRPTR)"ENV:MintPRINT/Unit0.cache",
                                 host, port, path);
    envarc_ok = mp_cache_write_file((CONST_STRPTR)"ENVARC:MintPRINT/Unit0.cache",
                                    host, port, path);

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
    CONST_STRPTR env_cache = (CONST_STRPTR)"ENV:MintPRINT/Unit0.cache";
    CONST_STRPTR envarc_cache = (CONST_STRPTR)"ENVARC:MintPRINT/Unit0.cache";

    if (!parse_ip_and_port(ip_buffer, host, sizeof(host), &port))
        return FALSE;
    if (port <= 0) port = 80;

    if (mp_cache_endpoint_matches(env_cache, host, port, driver_path_buffer))
        return mp_cache_load_file(env_cache);

    if (mp_cache_endpoint_matches(envarc_cache, host, port, driver_path_buffer))
        return mp_cache_load_file(envarc_cache);

    return FALSE;
}

static void apply_cached_capabilities(struct Window *win) {
    if (!win) return;

    update_media_dropdown(win);
    update_print_mode_dropdown(win);
    update_scaling_dropdown(win);
    update_quality_dropdown(win);

    /* Put the user's saved Unit0 choices back on top of the available lists. */
    apply_job_defaults_to_gadgets(win);
}



// Redirect printf to buffer
void custom_printf(const char *format, ...) {
    // Special case: clear the output area if the format string is "CLEAR"
    if (strcmp(format, "CLEAR") == 0) {
        output_line = 0;
        if (window) {
            struct RastPort *rp = window->RPort;
            if (font) SetFont(rp, font);
            SetAPen(rp, 1); // Text color
            SetBPen(rp, 0); // Background color
            SetDrMd(rp, JAM2);

            // Calculate the output area dimensions
            int line_height = font->tf_YSize + 2;
            int output_area_top = OUTPUT_TOP;
            int output_area_bottom = output_area_top + (MAX_OUTPUT_LINES * line_height) - 1;

            // Draw the border
            SetAPen(rp, 1); // Border color
            RectFill(rp, OUTPUT_LEFT - 2, output_area_top - 2, OUTPUT_RIGHT + 2, output_area_top - 1); // Top
            RectFill(rp, OUTPUT_LEFT - 2, output_area_bottom + 1, OUTPUT_RIGHT + 2, output_area_bottom + 2); // Bottom
            RectFill(rp, OUTPUT_LEFT - 2, output_area_top - 2, OUTPUT_LEFT - 1, output_area_bottom + 2); // Left
            RectFill(rp, OUTPUT_RIGHT + 1, output_area_top - 2, OUTPUT_RIGHT + 2, output_area_bottom + 2); // Right

            // Clear the output area
            SetAPen(rp, 0); // Background color
            RectFill(rp, OUTPUT_LEFT, output_area_top, OUTPUT_RIGHT, output_area_bottom);
        }
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

    // Refresh GUI output
    if (window) {
        struct RastPort *rp = window->RPort;
        if (font) SetFont(rp, font);
        SetAPen(rp, 1); // Text color
        SetBPen(rp, 0); // Background color
        SetDrMd(rp, JAM2);

        // Calculate the output area dimensions
        int line_height = font->tf_YSize + 2;
        int output_area_top = OUTPUT_TOP;
        int output_area_bottom = output_area_top + (MAX_OUTPUT_LINES * line_height) - 1;

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
        int start_line = (output_line > MAX_OUTPUT_LINES) ? (output_line - MAX_OUTPUT_LINES) : 0;
        for (int i = 0; i < MAX_OUTPUT_LINES && (start_line + i) < output_line; i++) {
            int y = output_area_top + (i * line_height) + font->tf_Baseline;
            Move(rp, OUTPUT_LEFT, y);
            SetAPen(rp, 1); // Text color
            Text(rp, output_buffer[start_line + i], strlen(output_buffer[start_line + i]));
        }
    }
}
// Override printf
#define printf custom_printf

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

    const char *requested = "media-source-supported,media-ready,printer-input-tray,printer-state,print-color-mode-supported,print-scaling-supported,print-quality-supported";
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

    // Receive response in chunks to avoid blocking
    printf("Waiting for response...\n");
    int total_received = 0;
    int max_attempts = 20; // 20 attempts at 100ms each = 2 seconds max
    int attempt = 0;

    while (total_received < maxlen - 1 && attempt < max_attempts) {
        ssize_t received = recv(sockfd, response + total_received, maxlen - 1 - total_received, 0);
        if (received > 0) {
            total_received += received;
            response[total_received] = '\0';
            if (strstr(response, "\r\n\r\n")) {
                break; // Got the full header and IPP payload
            }
        } else if (received == 0) {
            break; // Connection closed
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

    int content_len = 0;
    char *content_ptr = strstr(response, "Content-Length:");
    if (content_ptr) {
        sscanf(content_ptr, "Content-Length: %d", &content_len);
        printf("Content-Length: %d\n", content_len);
    } else {
        printf("No Content-Length header found\n");
    }

    // Find the start of the IPP payload
    char *ipp_start = strstr(response, "\r\n\r\n");
    if (!ipp_start) {
        printf("Failed to find IPP payload (no \\r\\n\\r\\n separator)\n");
        CloseSocket(sockfd);
        free(name);
        free(value);
        operation_in_progress = FALSE;
        return -1;
    }
    ipp_start += 4; // Skip the "\r\n\r\n"
    int ipp_len = total_received - (ipp_start - response);

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
    printf("query_printer_attributes completed\n");
    return 0;
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

    ssize_t received = recv(sockfd, response_buffer, 4096 - 1, 0);
    if (received > 0) {
        response_buffer[received] = '\0';
        char *ipp_start = strstr(response_buffer, "\r\n\r\n");
        if (ipp_start) {
            ipp_start += 4;
            printf("IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
        }
    } else {
        printf("No response or receive timeout.\n");
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

    ssize_t received = recv(sockfd, response_buffer, 4096 - 1, 0);
    if (received <= 0) {
        printf("No response or receive timeout.\n");
        CloseSocket(sockfd);
        free(response_buffer);
        operation_in_progress = FALSE;
        return -1;
    }

    response_buffer[received] = '\0';
    char *ipp_start = strstr(response_buffer, "\r\n\r\n");
    if (ipp_start) {
        ipp_start += 4;
        printf("IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
    } else {
        printf("Could not find IPP response payload.\n");
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

    // IP string gadget
    ng.ng_LeftEdge = 165;
    ng.ng_TopEdge = 5 + topborder;
    ng.ng_Width = 200;
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
    ng.ng_LeftEdge = 385;
    ng.ng_Width = 75;
    ng.ng_Height = 14;
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


    // Printer document engine. JPEG is the current working backend;
    // PWG Raster is persisted now and will be activated by the backend patch.
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 180;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Printer Engine:";
    ng.ng_GadgetID = GAD_ENGINE;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)engine_labels,
        GTCY_Active, strcmp(driver_engine_buffer, "pwg-raster") == 0 ? 1 : 0,
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
        GTCY_Labels, (ULONG)initial_media_labels,
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
                        
                            // Try default + fallback ports
                            int chosen_port = (port > 0) ? port : 80;
                            int ports_to_try[] = { chosen_port, 631 };
                        
                            for (int i = 0; i < 2; i++) {
                                printf("Trying %s:%d...\n", ip_only, ports_to_try[i]);
                                if (query_printer_attributes(ip_only, ports_to_try[i], response, MAX_BUFFER) == 0) {
                                    snprintf(ip_buffer, sizeof(ip_buffer), "%s:%d", ip_only, ports_to_try[i]);
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
                                    break; // Success!
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
                                printf("MintPRINT Unit0 saved to ENV: and ENVARC:\n");
                            else
                                printf("Failed to save MintPRINT Unit0 settings\n");
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
                                            printf("MintPRINT Unit0 saved to ENV: and ENVARC:\n");
                                        else
                                            printf("Failed to save MintPRINT Unit0 settings\n");
                                        break;
                    
                                    case 1: // Load Settings
                                        if (load_driver_config())
                                            printf("MintPRINT Unit0 loaded\n");
                                        else
                                            printf("No Unit0 found; using MintPRINT defaults\n");
                                        apply_driver_config_to_gadgets(win);
                                        apply_saved_option_state(win);
                                        apply_job_defaults_to_gadgets(win);
                                        load_print_mode();
                    
                                        // Update radio button state
                                        struct Gadget *print_mode_gadget = glist;
                                        while (print_mode_gadget && print_mode_gadget->GadgetID != GAD_PRINT_MODE)
                                            print_mode_gadget = print_mode_gadget->NextGadget;
                    
                                        if (print_mode_gadget && window) {
                                            GT_SetGadgetAttrs(print_mode_gadget, window, NULL,
                                                              GTCY_Active, print_mode,
                                                              TAG_DONE);
                                            RefreshGList(print_mode_gadget, window, NULL, 1);
                                            GT_RefreshWindow(window, NULL);
                                        }
                                        break;
                    
                                    case 3: // Quit
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
    printf("Window inner height: %d, topborder: %d\n", window ? window->Height - topborder : 0, topborder);
    print_mode_labels = initial_print_mode;
    scaling_mode_labels = initial_scaling_mode;
    quality_mode_labels = initial_quality_mode;
    // Load the same Unit0 profile used by DEVS:Printers/MintPRINT.
    load_driver_config();
    seed_saved_option_labels();

    // Load print mode from ENV:
    load_print_mode();
    
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
        WA_Title, (ULONG)"MintPRINT Preferences / Test",
        WA_Gadgets, (ULONG)glist,
        WA_AutoAdjust, TRUE,
        WA_Width, 460,
        WA_MinWidth, 460,
        WA_InnerHeight, 355,
        WA_MinHeight, 355,
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


    if (load_capability_cache_for_current_endpoint()) {
        apply_cached_capabilities(window);
        printf("Loaded cached printer capabilities\\n");
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

    // Free the dropdown labels
    if (media_dropdown_items) {
        for (int i = 0; media_dropdown_items[i]; i++) {
            FreeVec(media_dropdown_items[i]);
            media_dropdown_items[i] = NULL; // Safety
        }
        FreeVec(media_dropdown_items);
        media_dropdown_items = NULL;
    }
    cleanup_dropdown_labels();
    // Free the gadgets
    if (glist) {
        FreeGadgets(glist);
        glist = NULL;
    }

    // Close the window
    if (window) {
        CloseWindow(window);
        window = NULL;
    }

    // Unlock the screen
    if (screen) {
        UnlockPubScreen(NULL, screen);
        screen = NULL;
    }

    // Free VisualInfo (after unlocking the screen)
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }

    // Close the font
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