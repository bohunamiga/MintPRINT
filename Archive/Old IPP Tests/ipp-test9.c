/* ipptest9 - AmigaOS IPP Printer GUI Tool with Tray/Media Mapping (Grok-style) */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/bsdsocket.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>

#define USED __attribute__((used))
static const char USED min_stack[] = "$STACK:262144";

#define MAX_VALUES 32
#define MAX_ATTR_LEN 64
#define PORT 631
#define MAX_BUFFER 256000
#define MAX_OUTPUT_LINES 10
#define MAX_OUTPUT_LINE_LENGTH 47

#define GAD_IP_STRING 1
#define GAD_FILE_STRING 2
#define GAD_QUERY_BUTTON 3
#define GAD_PRINT_BUTTON 4
#define GAD_EXIT_BUTTON 5
#define GAD_MEDIA_DROPDOWN 6

#define OUTPUT_TOP     100
#define OUTPUT_LEFT    10
#define OUTPUT_LINE_H  8
#define OUTPUT_LINES   MAX_OUTPUT_LINES
#define OUTPUT_BOTTOM  (OUTPUT_TOP + (OUTPUT_LINE_H * OUTPUT_LINES) - 1)
#define OUTPUT_RIGHT   (window->Width - 10)

struct Window *window = NULL;
struct Gadget *glist = NULL;
struct Gadget *media_dropdown = NULL;
struct Screen *screen = NULL;
void *vi = NULL;
struct TextFont *font = NULL;
struct Library *SocketBase = NULL;
struct Library *GadToolsBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

char ip_buffer[64] = "192.168.0.44";
char file_buffer[256] = "UHD:test.jpg";
char output_buffer[MAX_OUTPUT_LINES][MAX_OUTPUT_LINE_LENGTH];
int output_line = 0;
BOOL operation_in_progress = FALSE;

struct TextAttr Topaz60 = { "topaz.font", 6, 0, 0 };

char supported_media_sources[MAX_VALUES][MAX_ATTR_LEN];
char supported_media[MAX_VALUES][MAX_ATTR_LEN];
int num_supported_media_sources = 0;
int num_supported_media = 0;

// Media-tray mapping based on Grok's interpretation
struct MediaTrayMap {
    char media[MAX_ATTR_LEN];   // e.g., "iso_a4_210x297mm"
    char tray[MAX_ATTR_LEN];    // e.g., "MP TRAY"
};
struct MediaTrayMap media_tray_map[MAX_VALUES];
int num_media_tray_mappings = 0;

// Dropdown
STRPTR *media_dropdown_items = NULL;

// Fallback if media-ready not present
BOOL has_media_ready = FALSE;

// Override printf to render in GUI
void custom_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char temp[256];
    vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    size_t len = strlen(temp);
    if (len > 0 && temp[len - 1] == '\n') temp[len - 1] = '\0';

    if (output_line >= MAX_OUTPUT_LINES) {
        for (int i = 0; i < MAX_OUTPUT_LINES - 1; i++)
            strncpy(output_buffer[i], output_buffer[i + 1], MAX_OUTPUT_LINE_LENGTH);
        output_line = MAX_OUTPUT_LINES - 1;
    }

    strncpy(output_buffer[output_line], temp, MAX_OUTPUT_LINE_LENGTH - 1);
    output_buffer[output_line][MAX_OUTPUT_LINE_LENGTH - 1] = '\0';
    output_line++;

    if (window) {
        struct RastPort *rp = window->RPort;
        if (font) SetFont(rp, font);
        SetAPen(rp, 1); SetBPen(rp, 0); SetDrMd(rp, JAM2);
        for (int i = 0; i < output_line; i++) {
            int y = 110 + i * 10;
            RectFill(rp, OUTPUT_LEFT, y - font->tf_YSize + 1, OUTPUT_RIGHT, y);
            Move(rp, 10, y);
            Text(rp, output_buffer[i], strlen(output_buffer[i]));
        }
    }
}
#define printf custom_printf

// Additional parsing, GUI logic, and printer query functions will follow in part 2.
// Media Tray Mapping (Grok-style)
void map_trays_to_media(void) {
    // Assume default fallback: 1:1 mapping based on order
    for (int i = 0; i < num_supported_media && i < num_supported_media_sources; i++) {
        strncpy(media_tray_map[i].media, supported_media[i], MAX_ATTR_LEN - 1);
        strncpy(media_tray_map[i].source, supported_media_sources[i], MAX_ATTR_LEN - 1);
        num_media_tray_mappings++;
    }
}

// Updated Query Function with Grok-style logic
int query_printer_attributes(const char *ip, char *response, int maxlen) {
    operation_in_progress = TRUE;
    num_supported_media = 0;
    num_supported_media_sources = 0;
    num_media_tray_mappings = 0;
    has_media_ready = FALSE;

    // Setup
    int sockfd;
    struct sockaddr_in serv_addr;
    char http_request[512];
    char ipp_payload[1024];
    char recv_buf[MAX_BUFFER];
    int offset = 0;

    // Build IPP request
    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp/print", ip);
    int uri_len = strlen(uri);

    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01; // IPP version
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B; // Get-Printer-Attributes
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01;

    ipp_payload[offset++] = 0x01; // Operation group
    #define ADD_STR_ATTR(tag, name, value) do { \
        int len = strlen(value); \
        ipp_payload[offset++] = tag; \
        ipp_payload[offset++] = 0x00; ipp_payload[offset++] = strlen(name); \
        memcpy(&ipp_payload[offset], name, strlen(name)); offset += strlen(name); \
        ipp_payload[offset++] = (len >> 8) & 0xFF; \
        ipp_payload[offset++] = len & 0xFF; \
        memcpy(&ipp_payload[offset], value, len); offset += len; \
    } while(0)

    ADD_STR_ATTR(0x47, "attributes-charset", "utf-8");
    ADD_STR_ATTR(0x48, "attributes-natural-language", "en");
    ADD_STR_ATTR(0x45, "printer-uri", uri);
    ADD_STR_ATTR(0x44, "requested-attributes", "media-source-supported");
    ADD_STR_ATTR(0x44, "requested-attributes", "media-ready");
    ADD_STR_ATTR(0x44, "requested-attributes", "printer-input-tray");

    ipp_payload[offset++] = 0x03; // End-of-attributes

    snprintf(http_request, sizeof(http_request),
             "POST /ipp/print HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             ip, offset);

    // Open socket
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) { operation_in_progress = FALSE; return -1; }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        CloseSocket(sockfd); operation_in_progress = FALSE; return -1;
    }

    // Send HTTP request + IPP payload
    send(sockfd, http_request, strlen(http_request), 0);
    send(sockfd, ipp_payload, offset, 0);

    // Receive response
    int total = 0;
    int received;
    while ((received = recv(sockfd, recv_buf + total, MAX_BUFFER - total, 0)) > 0) {
        total += received;
    }
    CloseSocket(sockfd);

    // Strip HTTP header
    char *ipp_start = strstr(recv_buf, "\r\n\r\n");
    if (!ipp_start) { operation_in_progress = FALSE; return -1; }
    ipp_start += 4;

    // Parse IPP block
    int pos = 8;
    char attr[64] = "", value[256];
    unsigned char *data = (unsigned char *)ipp_start;

    while (pos < total - 4) {
        unsigned char tag = data[pos++];
        if (tag == 0x03) break;

        int name_len = (data[pos] << 8) + data[pos + 1]; pos += 2;
        char name[64] = "";
        if (name_len > 0) { memcpy(name, &data[pos], name_len); name[name_len] = 0; pos += name_len; }
        else strcpy(name, attr);

        int val_len = (data[pos] << 8) + data[pos + 1]; pos += 2;
        if (val_len > 0) { memcpy(value, &data[pos], val_len); value[val_len] = 0; pos += val_len; }

        strcpy(attr, name);

        if (!strcmp(name, "media-source-supported")) {
            store_value(supported_media_sources, &num_supported_media_sources, value);
        } else if (!strcmp(name, "media-ready")) {
            store_value(supported_media, &num_supported_media, value);
            has_media_ready = TRUE;
        } else if (!strcmp(name, "printer-input-tray")) {
            // Optional parsing could be added here
        }
    }

    // Map media to sources
    if (has_media_ready) {
        for (int i = 0; i < num_supported_media && i < num_supported_media_sources; i++) {
            strncpy(media_tray_map[i].media, supported_media[i], MAX_ATTR_LEN - 1);
            strncpy(media_tray_map[i].source, supported_media_sources[i], MAX_ATTR_LEN - 1);
            num_media_tray_mappings++;
        }
    } else {
        // Fallback: use media-supported with default "auto" tray
        for (int i = 0; i < num_supported_media; i++) {
            strncpy(media_tray_map[i].media, supported_media[i], MAX_ATTR_LEN - 1);
            strncpy(media_tray_map[i].source, "auto", MAX_ATTR_LEN - 1);
            num_media_tray_mappings++;
        }
    }

    printf("Mapped %d trays to media.\n", num_media_tray_mappings);
    update_media_dropdown(window);
    operation_in_progress = FALSE;
    return 0;
}
void update_media_dropdown(struct Window *win) {
    printf("Updating media dropdown, num_mappings=%d\n", num_media_tray_mappings);

    if (media_dropdown_items) {
        for (int i = 0; media_dropdown_items[i]; i++) FreeVec(media_dropdown_items[i]);
        FreeVec(media_dropdown_items);
        media_dropdown_items = NULL;
    }

    if (num_media_tray_mappings == 0) return;

    media_dropdown_items = AllocVec((num_media_tray_mappings + 1) * sizeof(STRPTR), MEMF_CLEAR);
    if (!media_dropdown_items) return;

    for (int i = 0; i < num_media_tray_mappings; i++) {
        char label[MAX_ATTR_LEN * 2];
        snprintf(label, sizeof(label), "%s (%s)", media_tray_map[i].media, media_tray_map[i].source);
        media_dropdown_items[i] = AllocVec(strlen(label) + 1, MEMF_CLEAR);
        if (media_dropdown_items[i]) strcpy(media_dropdown_items[i], label);
    }
    media_dropdown_items[num_media_tray_mappings] = NULL;

    if (media_dropdown) {
        GT_SetGadgetAttrs(media_dropdown, win, NULL,
                          GTCY_Labels, (ULONG)media_dropdown_items,
                          GTCY_Active, 0,
                          TAG_DONE);
        RefreshGList(media_dropdown, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}
