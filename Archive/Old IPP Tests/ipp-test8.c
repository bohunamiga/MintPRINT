/* Amiga IPP Print-Job Prototype with GUI
   Sends a JPEG file to an IPP printer (AirPrint-compatible)
   Compile with: m68k-amigaos-gcc -g -o IPP-test3 ipp-test3.c -lamiga -lsocket -lm
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
typedef long ssize_t;
#include <proto/bsdsocket.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for O_NONBLOCK

#define MAX_VALUES 32
#define MAX_ATTR_LEN 64
#define PORT 631
#define MAX_BUFFER 256000
#define MAX_OUTPUT_LINES 10
#define MAX_OUTPUT_LINE_LENGTH 47

// Gadget IDs
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

// Define the USED macro for GCC
#define USED __attribute__((used))

// Stack cookie to request a minimum stack size of 100 KB (102,400 bytes)
static const char USED min_stack[] = "$STACK:262144";

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

// Media dropdown state
char *selected_media = NULL;
struct Gadget *media_dropdown = NULL;
STRPTR *media_dropdown_items = NULL;
BOOL has_media_ready = FALSE;
// Structure to map media sizes to trays
struct MediaTrayMap {
    char media[MAX_ATTR_LEN];      // e.g., "iso_a3_297x420mm"
    char source[MAX_ATTR_LEN];     // e.g., "tray-1"
};
struct MediaTrayMap media_tray_map[MAX_VALUES];
int num_media_tray_mappings = 0;
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
//Media Size Helper
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

// Global variables for GUI
struct Window *window = NULL;
struct Gadget *glist = NULL;
struct Library *SocketBase = NULL;
struct Library *GadToolsBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
char ip_buffer[64] = "192.168.0.44";
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

// Add after successful query to rebuild media dropdown
void update_media_dropdown(struct Window *win) {
    printf("Updating media dropdown, num_mappings=%d\n", num_media_tray_mappings);

    // Free old items
    if (media_dropdown_items) {
        for (int i = 0; media_dropdown_items[i]; i++) FreeVec(media_dropdown_items[i]);
        FreeVec(media_dropdown_items);
        media_dropdown_items = NULL;
    }

    if (num_media_tray_mappings == 0) {
        printf("No media-tray mappings found.\n");
        return;
    }

    // Allocate new array
    media_dropdown_items = AllocVec((num_media_tray_mappings + 1) * sizeof(STRPTR), MEMF_CLEAR);
    if (!media_dropdown_items) {
        printf("Failed to allocate media dropdown items\n");
        return;
    }

    // Fill with "Media (Tray)" format
    for (int i = 0; i < num_media_tray_mappings; i++) {
        char entry[MAX_ATTR_LEN + 20];
        snprintf(entry, sizeof(entry), "%s (%s)", media_tray_map[i].media, media_tray_map[i].source);
        media_dropdown_items[i] = AllocVec(strlen(entry) + 1, MEMF_CLEAR);
        if (media_dropdown_items[i]) {
            strcpy(media_dropdown_items[i], entry);
            printf("Dropdown item %d: %s\n", i, media_dropdown_items[i]);
        }
    }
    media_dropdown_items[num_media_tray_mappings] = NULL;

    // Update gadget
    if (media_dropdown) {
        GT_SetGadgetAttrs(media_dropdown, win, NULL,
                          GTCY_Labels, (ULONG)media_dropdown_items,
                          GTCY_Active, 0,
                          TAG_DONE);
        RefreshGList(media_dropdown, win, NULL, 1);
        GT_RefreshWindow(win, NULL);
    }
}

// Redirect printf to buffer
void custom_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    char temp[256];
    vsnprintf(temp, sizeof(temp), format, args);
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

    // Refresh GUI output
    if (window) {
        struct RastPort *rp = window->RPort;
        if (font) SetFont(rp, font);
        SetAPen(rp, 1); // Text
        SetBPen(rp, 0); // Background
        SetDrMd(rp, JAM2);

        // Clear and redraw each line
        for (int i = 0; i < output_line; i++) {
            int y = 110 + i * 10;
            RectFill(rp, OUTPUT_LEFT, y - font->tf_YSize + 1, OUTPUT_RIGHT, y);
            Move(rp, 10, y);
            Text(rp, output_buffer[i], strlen(output_buffer[i]));
        }
    }
}


// Override printf
#define printf custom_printf

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

int query_printer_attributes(const char *ip, char *response, int maxlen) {
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
    num_media_tray_mappings = 0;
    has_media_ready = FALSE;

    // Open log file (temporarily disabled to rule out I/O issues)
    /*
    FILE *log_file = fopen("ipp_log.txt", "w");
    if (!log_file) {
        printf("Failed to open log file\n");
        operation_in_progress = FALSE;
        return -1;
    }
    */
    FILE *log_file = NULL; // Disable logging for now

    // Allocate buffers
    char *name = malloc(512);
    char *value = malloc(512);
    if (!name || !value) {
        printf("Memory allocation failed\n");
        if (name) free(name);
        if (value) free(value);
        if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    // Build IPP payload
    static unsigned char ipp_payload[2048];
    int offset = 0;
    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp", ip);
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

    const char *requested = "media-ready,media-source-supported,media-supported,printer-state";
    int requested_len = strlen(requested);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "requested-attributes", 18); offset += 18;
    ipp_payload[offset++] = (requested_len >> 8) & 0xFF;
    ipp_payload[offset++] = requested_len & 0xFF;
    memcpy(&ipp_payload[offset], requested, requested_len); offset += requested_len;

    ipp_payload[offset++] = 0x03; // End of attributes

    // Build HTTP header
    static char http_header[256];
    snprintf(http_header, sizeof(http_header),
             "POST /ipp HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             ip, offset);

    // Open socket
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        snprintf(response, maxlen, "Socket creation failed");
        free(name); free(value); if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    // Set a very short timeout to minimize blocking
    struct timeval timeout = {1, 0}; // 1-second timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        snprintf(response, maxlen, "Invalid IP address: %s", ip);
        CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    printf("Connecting to printer...\n");
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        if (errno != EINPROGRESS) {
            snprintf(response, maxlen, "Failed to connect to printer");
            CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
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
        CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
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
                CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
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
        CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    response[total_received] = '\0';
    int content_len = 0;
    char *content_ptr = strstr(response, "Content-Length:");
    if (content_ptr) {
        sscanf(content_ptr, "Content-Length: %d", &content_len);
    }
    
    // Now find the actual binary payload
    char *ipp_start = strstr(response, "\r\n\r\n");
    if (!ipp_start || content_len <= 0) {
        printf("Failed to find IPP payload or valid length\n");
        return -1;
    }
    ipp_start += 4; // skip HTTP header end
    int ipp_len = total_received - (ipp_start - response);

    if (ipp_len < 8) {
        printf("IPP response too short: %d bytes\n", ipp_len);
        CloseSocket(sockfd); free(name); free(value); if (log_file) fclose(log_file);
        operation_in_progress = FALSE;
        return -1;
    }

    printf("IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
    //if (log_file) fprintf(log_file, "IPP Status: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
    //if (log_file) fflush(log_file);

    int pos = 8; // Skip header
    int attributes_processed = 0;
    int last_pos = pos;
    int max_attributes = 1000; // Safety limit to prevent infinite loops

    while (pos < ipp_len && attributes_processed < max_attributes) {
        // Check for parsing stall (infinite loop protection)
        printf("IPP payload (%d bytes):\n", ipp_len);
for (int i = 0; i < ipp_len; i++) {
    printf("%02x ", (unsigned char)ipp_start[i]);
    if ((i + 1) % 16 == 0) printf("\n");
}
printf("\n");
        if (pos == last_pos) {
            printf("Parsing stuck at pos %d, aborting\n", pos);
            //if (log_file) fprintf(log_file, "Parsing stuck at pos %d, aborting\n", pos);
            break;
        }
        last_pos = pos;

        unsigned char tag = ipp_start[pos++];
        if (tag == 0x03) break; // End of attributes

        if (tag >= 0x01 && tag <= 0x05) { // Attribute group
            while (pos < ipp_len && ipp_start[pos] > 0x05) {
                unsigned char value_tag = ipp_start[pos++];
                if (pos + 2 > ipp_len) {
                    printf("Invalid name length position at pos %d, tag 0x%02x\n", pos, value_tag);
                    break;
                }
                int name_len = (ipp_start[pos] << 8) | ipp_start[pos + 1]; pos += 2;
                if (name_len < 0 || name_len >= 512 || pos + name_len > ipp_len) {
                    printf("Invalid name length: %d at pos %d, tag 0x%02x\n", name_len, pos, value_tag);
                    break;
                }
                strncpy(name, ipp_start + pos, name_len); name[name_len] = '\0'; pos += name_len;

                if (pos + 2 > ipp_len) {
                    printf("Invalid value length position at pos %d for %s, tag 0x%02x\n", pos, name, value_tag);
                    break;
                }
                int value_len = (ipp_start[pos] << 8) | ipp_start[pos + 1]; pos += 2;
                if (value_len < 0 || value_len >= 512 || pos + value_len > ipp_len) {
                    printf("Invalid value length: %d for %s at pos %d, tag 0x%02x\n", value_len, name, pos, value_tag);
                    break;
                }

                // Handle collection values (e.g., media-col-ready)
                if (value_tag == 0x34 || value_tag == 0x37) { // Begin collection or end collection
                    // Skip nested collections for now to avoid parsing issues
                    printf("Skipping collection attribute: %s at pos %d\n", name, pos);
                    pos += value_len;
                } else {
                    strncpy(value, ipp_start + pos, value_len); value[value_len] = '\0'; pos += value_len;

                    // Handle attributes
                    if (strcmp(name, "media-ready") == 0 && value_tag == 0x44) {
                        has_media_ready = TRUE;
                        store_value(supported_media, &num_supported_media, value);
                        if (num_media_tray_mappings < MAX_VALUES) {
                            strncpy(media_tray_map[num_media_tray_mappings].media, value, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].source, "auto", MAX_ATTR_LEN - 1);
                            num_media_tray_mappings++;
                        }
                        //if (log_file) fprintf(log_file, "Attr: %s = %s\n", name, value);
                    } else if (strcmp(name, "media-source-supported") == 0 && value_tag == 0x44) {
                        store_value(supported_media_sources, &num_supported_media_sources, value);
                        //if (log_file) fprintf(log_file, "Attr: %s = %s\n", name, value);
                    } else if (!has_media_ready && strcmp(name, "media-supported") == 0 && value_tag == 0x44) {
                        store_value(supported_media, &num_supported_media, value);
                        if (num_media_tray_mappings < MAX_VALUES) {
                            strncpy(media_tray_map[num_media_tray_mappings].media, value, MAX_ATTR_LEN - 1);
                            strncpy(media_tray_map[num_media_tray_mappings].source, "auto", MAX_ATTR_LEN - 1);
                            num_media_tray_mappings++;
                        }
                        //if (log_file) fprintf(log_file, "Attr: %s = %s\n", name, value);
                    } else if (strcmp(name, "printer-state") == 0 && value_tag == 0x21 && value_len == 4) {
                        int state = (ipp_start[pos - value_len] << 24) |
                                    (ipp_start[pos - value_len + 1] << 16) |
                                    (ipp_start[pos - value_len + 2] << 8) |
                                    ipp_start[pos - value_len + 3];
                        printf("Printer state: %d\n", state);
                        //if (log_file) fprintf(log_file, "Attr: %s = %d\n", name, state);
                    }
                }

                // Process GUI events for every attribute to keep the GUI responsive
                attributes_processed++;
                if (window) {
                    struct IntuiMessage *imsg;
                    while ((imsg = GT_GetIMsg(window->UserPort))) {
                        GT_ReplyIMsg(imsg);
                    }
                }
            }
        }
    }

    if (attributes_processed >= max_attributes) {
        printf("Reached maximum attribute limit (%d), aborting parsing\n", max_attributes);
    }

    // Map media to trays (simplified: match by index for now)
    for (int i = 0; i < num_media_tray_mappings && i < num_supported_media_sources; i++) {
        strncpy(media_tray_map[i].source, supported_media_sources[i], MAX_ATTR_LEN - 1);
    }

    // Print results
    printf("Media-Tray Mappings:\n");
    for (int i = 0; i < num_media_tray_mappings; i++) {
        printf("- %s -> %s\n", media_tray_map[i].media, media_tray_map[i].source);
    }
    printf("Supported Sources:\n");
    for (int i = 0; i < num_supported_media_sources; i++) {
        printf("- %s\n", supported_media_sources[i]);
    }

    // Cleanup
    CloseSocket(sockfd);
    free(name);
    free(value);
    if (log_file) fclose(log_file);
    operation_in_progress = FALSE;

    if (window && vi) update_media_dropdown(window);
    return 0;
}

int send_print_job(const char *ip, const char *filename, const char *media) {
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
    printf("Selected media: %s, source: %s\n", media, selected_source);

    struct sockaddr_in serv_addr;
    int sockfd = -1;
    static unsigned char ipp_payload[2048];
    int offset = 0;

    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp", ip);
    int uri_len = strlen(uri);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open file: %s\n", filename);
        operation_in_progress = FALSE;
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        printf("Failed to allocate memory for file data\n");
        fclose(file);
        operation_in_progress = FALSE;
        return -1;
    }
    fread(file_data, 1, file_size, file);
    fclose(file);

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

    // Add media attribute
    int media_len = strlen(media);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "media", 5); offset += 5;
    ipp_payload[offset++] = (media_len >> 8) & 0xFF;
    ipp_payload[offset++] = media_len & 0xFF;
    memcpy(&ipp_payload[offset], media, media_len); offset += media_len;

    // Add media-source attribute
    int source_len = strlen(selected_source);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0c;
    memcpy(&ipp_payload[offset], "media-source", 12); offset += 12;
    ipp_payload[offset++] = (source_len >> 8) & 0xFF;
    ipp_payload[offset++] = source_len & 0xFF;
    memcpy(&ipp_payload[offset], selected_source, source_len); offset += source_len;

    ipp_payload[offset++] = 0x03; // End of attributes

    static char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        ip, offset + file_size);

    printf("Sending JPEG to printer at %s...\n", ip);
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("Socket creation failed.\n");
        free(file_data);
        operation_in_progress = FALSE;
        return -1;
    }

    struct timeval timeout = {2, 0}; // 2-second timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        CloseSocket(sockfd);
        free(file_data);
        operation_in_progress = FALSE;
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address: %s\n", ip);
        CloseSocket(sockfd);
        free(file_data);
        operation_in_progress = FALSE;
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect to printer.\n");
        CloseSocket(sockfd);
        free(file_data);
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

    if (send(sockfd, http_header, strlen(http_header), 0) < 0 ||
        send(sockfd, (char *)ipp_payload, offset, 0) < 0 ||
        send(sockfd, file_data, file_size, 0) < 0) {
        printf("Failed sending data.\n");
        CloseSocket(sockfd);
        free(file_data);
        operation_in_progress = FALSE;
        return -1;
    }

    free(file_data);

    printf("Waiting for response...\n");
    char response_buffer[4096];
    ssize_t received = recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    if (received <= 0) {
        printf("No response or receive timeout.\n");
        CloseSocket(sockfd);
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

    CloseSocket(sockfd);
    operation_in_progress = FALSE;
    return 0;
}

// Function to create all GadTools gadgets
struct Gadget *createAllGadgets(struct Gadget **glistptr, void *vi, UWORD topborder) {
    struct NewGadget ng;
    struct Gadget *gad;
    static const STRPTR initial_labels[] = { "Select Media...", NULL };

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
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge = 5 + topborder;
    ng.ng_Width = 200;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"_Printer IP:";
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

    // Media dropdown
    ng.ng_LeftEdge = 130;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 240;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"Media:";
    ng.ng_GadgetID = GAD_MEDIA_DROPDOWN;
    ng.ng_Flags = NG_HIGHLABEL;
    gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)initial_labels,
        GTCY_Active, 0,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create media dropdown\n");
        return NULL;
    }
    media_dropdown = gad;  // Save it globally

    // File path string gadget
    ng.ng_TopEdge += 20;
    ng.ng_GadgetText = (STRPTR)"_File Path:";
    ng.ng_GadgetID = GAD_FILE_STRING;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)file_buffer,
        GTST_MaxChars, sizeof(file_buffer) - 1,
        GA_Immediate, TRUE,
        GT_Underscore, '_',
        GACT_RELVERIFY, TRUE,
        TAG_DONE);
    if (!gad) {
        printf("Failed to create file string gadget\n");
        return NULL;
    }

    // Query button
    ng.ng_LeftEdge = 10;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 110;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"_Query Printer";
    ng.ng_GadgetID = GAD_QUERY_BUTTON;
    ng.ng_Flags = 0;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create query button\n");
        return NULL;
    }

    // Print button
    ng.ng_LeftEdge += 120;
    ng.ng_GadgetText = (STRPTR)"_Print File";
    ng.ng_GadgetID = GAD_PRINT_BUTTON;
    gad = CreateGadget(BUTTON_KIND, gad, &ng,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create print button\n");
        return NULL;
    }

    // Exit button
    ng.ng_LeftEdge += 120;
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
    char response[MAX_BUFFER];

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
                            if (selected < (ULONG)num_supported_media) {
                                printf("selected index = %lu, value = %s\n",
                                       selected, supported_media[selected]);
                            } else {
                                printf("invalid selection index = %lu\n", selected);
                            }
                        }
                        break;
                        case GAD_IP_STRING:
                            // Retrieve the current string from the gadget
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

                            case GAD_FILE_STRING:
                            {
                                GT_RefreshWindow(win, NULL);  // Ensure the string gadget is up to date
                            
                                char *current_file = NULL;
                                ULONG success = GT_GetGadgetAttrs(gad, win, NULL,
                                                                  GTST_String, (ULONG)&current_file,
                                                                  TAG_DONE);
                                if (success && current_file) {
                                    printf("Raw file string from gadget: '%s'\n", current_file);
                                    strncpy(file_buffer, current_file, sizeof(file_buffer) - 1);
                                    file_buffer[sizeof(file_buffer) - 1] = '\0';
                                    printf("File buffer after update: '%s'\n", file_buffer);
                                } else {
                                    printf("Failed to retrieve file string from gadget\n");
                                }
                            }
                            break;

                            case GAD_QUERY_BUTTON:
                            {
                                GT_RefreshWindow(win, NULL);  // Force GadTools to flush input
                            
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
                                        printf("Got IP string from gadget: '%s'\n", ip_string);
                                        strncpy(ip_buffer, ip_string, sizeof(ip_buffer) - 1);
                                        ip_buffer[sizeof(ip_buffer) - 1] = '\0';
                                        printf("IP buffer updated to: '%s'\n", ip_buffer);
                                    } else {
                                        printf("GT_GetGadgetAttrs() failed or null result\n");
                                    }
                                } else {
                                    printf("IP string gadget not found!\n");
                                }
                            
                                printf("Querying printer attributes at %s...\n", ip_buffer);
                                if (query_printer_attributes(ip_buffer, response, sizeof(response)) == 0) {
                                    printf("Printer Attributes Response:\n%s\n", response);
                                } else {
                                    printf("Error querying attributes: %s\n", response);
                                }
                            }
                            break;

                            case GAD_PRINT_BUTTON:
                            {
                                GT_RefreshWindow(win, NULL);  // Flush string gadgets
                                 
                                // Get IP string
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
                                    } else {
                                        printf("Failed to retrieve IP string from gadget\n");
                                    }
                                } else {
                                    printf("IP string gadget not found!\n");
                                }

                                // Fetch selected media value (like IP string)
                                ULONG media_index = 0;
                                GT_GetGadgetAttrs(media_dropdown, win, NULL,
                                                GTCY_Active, (ULONG)&media_index,
                                                TAG_DONE);

                                if (media_index >= (ULONG)num_supported_media) {
                                    printf("Invalid media dropdown index: %lu\n", media_index);
                                    break;
                                }

                                const char *media_str = supported_media[media_index];
                                printf("Selected media from dropdown: %s\n", media_str);
                            
                                // Get File path string
                                struct Gadget *file_gadget = glist;
                                while (file_gadget && file_gadget->GadgetID != GAD_FILE_STRING) {
                                    file_gadget = file_gadget->NextGadget;
                                }
                            
                                if (file_gadget) {
                                    char *file_string = NULL;
                                    ULONG success = GT_GetGadgetAttrs(file_gadget, win, NULL,
                                                                      GTST_String, (ULONG)&file_string,
                                                                      TAG_DONE);
                                    if (success && file_string) {
                                        strncpy(file_buffer, file_string, sizeof(file_buffer) - 1);
                                        file_buffer[sizeof(file_buffer) - 1] = '\0';
                                        printf("File buffer updated to: '%s'\n", file_buffer);
                                    } else {
                                        printf("Failed to retrieve file string from gadget\n");
                                    }
                                } else {
                                    printf("File path gadget not found!\n");
                                }
                            
                                // Send print job
                                printf("Sending file to printer at %s...\n", ip_buffer);
                                send_print_job(ip_buffer, file_buffer, media_str);
                            }
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
            }

            imsg = GT_GetIMsg(win->UserPort);
        }
    }
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
    printf("Window inner height: %d, topborder: %d\n", window->Height - topborder, topborder);
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
        WA_Title, (ULONG)"Amiga IPP Printer",
        WA_Gadgets, (ULONG)glist,
        WA_AutoAdjust, TRUE,
        WA_Width, 400,
        WA_MinWidth, 60,
        WA_InnerHeight, 220,
        WA_MinHeight, 60,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP,
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
        if (media_dropdown) {
            RemoveGList(window, media_dropdown, 1);
            FreeGadgets(media_dropdown);
            media_dropdown = NULL;
        }
        return 1;
    }

    // Refresh window
    GT_RefreshWindow(window, NULL);

    // Process events
    process_window_events(window);

    // Cleanup
    if (media_dropdown) {
        RemoveGList(window, media_dropdown, -1);
        FreeGadgets(media_dropdown);
        media_dropdown = NULL;
    }
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }
    if (window) {
        CloseWindow(window);
        window = NULL;
    }
    // 2. Free the dropdown labels
    if (media_dropdown_items) {
        for (int i = 0; media_dropdown_items[i]; i++) {
            FreeVec(media_dropdown_items[i]);
        }
        FreeVec(media_dropdown_items);
        media_dropdown_items = NULL;
    }
    
    // 3. Free the gadgets
    if (glist) {
        FreeGadgets(glist);
        glist = NULL;
    }
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, screen);
    CloseFont(font);
    CloseLibrary(SocketBase);
    CloseLibrary(GadToolsBase);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}