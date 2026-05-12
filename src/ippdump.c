#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 631
#define MAX_RESPONSE 15000
#define MAX_ITEMS 32
char media_ready[MAX_ITEMS][64];
int num_media_ready = 0;

char media_sources[MAX_ITEMS][64];
int num_media_sources = 0;
struct Library *SocketBase;

// Parses and prints IPP attributes from raw IPP payload
void parse_ipp_attributes(const unsigned char *ipp_data, int ipp_len) {
    int pos = 8; // Skip IPP header
    int last_pos = -1;
    int iter = 0, max_iter = 10000;
    char name[256], value[256];

    printf("\n--- Decoding IPP Attributes ---\n");

    while (pos < ipp_len && iter++ < max_iter) {
        if (pos == last_pos) {
            printf("Parsing stuck at pos %d, aborting\n", pos);
            break;
        }
        last_pos = pos;

        unsigned char tag = ipp_data[pos++];
        if (tag == 0x03) {
            printf("End-of-attributes tag found\n");
            break;
        }
        if (tag >= 0x01 && tag <= 0x05) continue; // group start

        if (pos + 2 > ipp_len) break;
        int name_len = (ipp_data[pos] << 8) | ipp_data[pos + 1];
        pos += 2;

        if (pos + name_len > ipp_len || name_len >= sizeof(name)) break;
        if (name_len > 0) {
            memcpy(name, &ipp_data[pos], name_len);
            name[name_len] = '\0';
            pos += name_len;
        } else {
            strcpy(name, "(additional)");
        }

        if (pos + 2 > ipp_len) break;
        int value_len = (ipp_data[pos] << 8) | ipp_data[pos + 1];
        pos += 2;

        if (pos + value_len > ipp_len || value_len >= sizeof(value)) break;
        memcpy(value, &ipp_data[pos], value_len);
        value[value_len] = '\0';
        pos += value_len;

        if (strcmp(name, "media-ready") == 0 && num_media_ready < MAX_ITEMS) {
            strncpy(media_ready[num_media_ready++], value, 63);
        }
        else if (strcmp(name, "media-source-supported") == 0 && num_media_sources < MAX_ITEMS) {
            strncpy(media_sources[num_media_sources++], value, 63);
        }
        else {
           //printf("Attr: %s = %s\n", name, value);
            if (strcmp(name, "media-col-ready") == 0) {
                printf("Found media-col-ready!\n");
            }
        }
    }
    printf("\n--- Media Tray Mapping ---\n");
    int max = num_media_ready > num_media_sources ? num_media_sources : num_media_ready;
    for (int i = 0; i < max; i++) {
        printf("- %s in %s\n", media_ready[i], media_sources[i]);
    }
    printf("--- Done decoding ---\n");
}

void parse_media_col_ready(const unsigned char *ipp_data, int ipp_len) {
    int pos = 8; // Skip IPP header
    int last_pos = -1;
    int iter = 0;
    int max_iter = 10000;

    char current_media[256] = "(unknown)";
    char current_source[256] = "(unknown)";
    BOOL inside_media_col = FALSE;
    BOOL inside_media_size = FALSE;
    int x_dim = 0, y_dim = 0;

    BPTR debug_file = Open("media_col_debug.txt", MODE_NEWFILE);
    if (!debug_file) {
        printf("Failed to open debug file media_col_debug.txt\n");
        return;
    }

    printf("\n--- Media Tray Mappings (media-col-ready) ---\n");
    FPrintf(debug_file, (STRPTR)"--- Media Tray Mappings (media-col-ready) ---\n");

    while (pos < ipp_len && iter++ < max_iter) {
        if (pos == last_pos) {
            printf("Parser stuck at pos %d, exiting.\n", pos);
            FPrintf(debug_file, (STRPTR)"Parser stuck at pos %d, exiting.\n", pos);
            break;
        }
        last_pos = pos;

        unsigned char tag = ipp_data[pos++];
        FPrintf(debug_file, (STRPTR)"Pos %d: Tag 0x%02X\n", pos - 1, tag);

        if (tag == 0x03) {
            FPrintf(debug_file, (STRPTR)"End of attributes\n");
            break;
        }

        if (tag >= 0x01 && tag <= 0x05) {
            FPrintf(debug_file, (STRPTR)"Group tag, skipping\n");
            continue;
        }

        if (pos + 2 > ipp_len) {
            FPrintf(debug_file, (STRPTR)"Not enough data for name length\n");
            break;
        }
        int name_len = (ipp_data[pos] << 8) | ipp_data[pos + 1];
        pos += 2;
        FPrintf(debug_file, (STRPTR)"Name length: %d\n", name_len);

        char name[256] = "";
        if (name_len > 0 && name_len < sizeof(name) && pos + name_len <= ipp_len) {
            memcpy(name, &ipp_data[pos], name_len);
            name[name_len] = '\0';
        } else if (name_len == 0) {
            strcpy(name, "(additional)");
        } else {
            FPrintf(debug_file, (STRPTR)"Invalid name length or out of bounds\n");
            break;
        }
        pos += name_len;
        FPrintf(debug_file, (STRPTR)"Name: %s\n", (LONG)name);

        if (pos + 2 > ipp_len) {
            FPrintf(debug_file, (STRPTR)"Not enough data for value length\n");
            break;
        }
        int value_len = (ipp_data[pos] << 8) | ipp_data[pos + 1];
        pos += 2;
        FPrintf(debug_file, (STRPTR)"Value length: %d\n", value_len);

        if (value_len < 0 || pos + value_len > ipp_len) {
            FPrintf(debug_file, (STRPTR)"Invalid value length or out of bounds\n");
            break;
        }

        const unsigned char *value_ptr = &ipp_data[pos];
        char value[256] = "";
        if (value_len < sizeof(value)) {
            memcpy(value, value_ptr, value_len);
            value[value_len] = '\0';
        }
        FPrintf(debug_file, (STRPTR)"Raw value bytes: ");
        for (int i = 0; i < value_len; i++) {
            FPrintf(debug_file, (STRPTR)"%02X ", value_ptr[i]);
        }
        FPrintf(debug_file, (STRPTR)"\n");
        FPrintf(debug_file, (STRPTR)"Value (as string): %s\n", (LONG)value);
        pos += value_len;

        // Handle start of media-col-ready collection
        if (tag == 0x34 && strcmp(name, "media-col-ready") == 0) {
            inside_media_col = TRUE;
            strcpy(current_media, "(unknown)");
            strcpy(current_source, "(unknown)");
            x_dim = 0;
            y_dim = 0;
            FPrintf(debug_file, (STRPTR)"Entering media-col-ready collection\n");
            continue;
        }

        // Handle start of media-size sub-collection
        if (inside_media_col && tag == 0x34 && strcmp(name, "media-size") == 0) {
            inside_media_size = TRUE;
            FPrintf(debug_file, (STRPTR)"Entering media-size sub-collection\n");
            continue;
        }

        // Handle attributes inside collections
        if (inside_media_col) {
            if (inside_media_size) {
                if (tag == 0x21) { // Integer tag for x-dimension and y-dimension
                    int int_value = 0;
                    if (value_len == 4) {
                        int_value = (value_ptr[0] << 24) | (value_ptr[1] << 16) |
                                    (value_ptr[2] << 8) | value_ptr[3];
                    }
                    FPrintf(debug_file, (STRPTR)"Integer value: %d\n", int_value);
                    if (strcmp(name, "x-dimension") == 0) {
                        x_dim = int_value;
                        FPrintf(debug_file, (STRPTR)"Set x_dim to %d\n", x_dim);
                    } else if (strcmp(name, "y-dimension") == 0) {
                        y_dim = int_value;
                        FPrintf(debug_file, (STRPTR)"Set y_dim to %d\n", y_dim);
                    }
                }
            } else {
                // Fallback to media or media-size-name if dimensions are not available
                if (strcmp(name, "media") == 0 || strcmp(name, "media-size-name") == 0) {
                    strncpy(current_media, value, sizeof(current_media) - 1);
                    current_media[sizeof(current_media) - 1] = '\0';
                    // Simplify media name for output
                    if (strcmp(current_media, "iso_a4_210x297mm") == 0) {
                        strcpy(current_media, "A4");
                    } else if (strcmp(current_media, "iso_a3_297x420mm") == 0) {
                        strcpy(current_media, "A3");
                    }
                    FPrintf(debug_file, (STRPTR)"Updated current_media to: %s\n", (LONG)current_media);
                } else if (strcmp(name, "media-source") == 0) {
                    strncpy(current_source, value, sizeof(current_source) - 1);
                    current_source[sizeof(current_source) - 1] = '\0';
                    // Capitalize tray name for consistency
                    if (strcmp(current_source, "tray-1") == 0) {
                        strcpy(current_source, "Tray1");
                    } else if (strcmp(current_source, "tray-2") == 0) {
                        strcpy(current_source, "Tray2");
                    }
                    FPrintf(debug_file, (STRPTR)"Updated current_source to: %s\n", (LONG)current_source);
                }
            }
        }

        // Handle end of media-size sub-collection
        if (tag == 0x37 && inside_media_size) {
            inside_media_size = FALSE;
            // Format the media size if dimensions were found
            if (x_dim == 21000 && y_dim == 29700) {
                snprintf(current_media, sizeof(current_media), "A4");
            } else if (x_dim == 29700 && y_dim == 42000) {
                snprintf(current_media, sizeof(current_media), "A3");
            } else if (x_dim != 0 && y_dim != 0) {
                snprintf(current_media, sizeof(current_media), "%dx%dmm", x_dim / 100, y_dim / 100);
            }
            FPrintf(debug_file, (STRPTR)"Exiting media-size sub-collection, formatted current_media: %s\n", (LONG)current_media);
            continue;
        }

        // Handle end of media-col-ready collection
        if (tag == 0x37 && inside_media_col && !inside_media_size) {
            inside_media_col = FALSE;
            // Only print if we have meaningful data
            if (strcmp(current_media, "(unknown)") != 0 || strcmp(current_source, "(unknown)") != 0) {
                printf("Media: %s -> Tray: %s\n", current_media, current_source);
                FPrintf(debug_file, (STRPTR)"Exiting media-col-ready collection\n");
                FPrintf(debug_file, (STRPTR)"Media: %s -> Tray: %s\n", (LONG)current_media, (LONG)current_source);
            }
        }
    }

    printf("--- Done parsing media-col-ready ---\n");
    FPrintf(debug_file, (STRPTR)"--- Done parsing media-col-ready ---\n");
    Close(debug_file);
}

int main(int argc, char **argv) {
    struct RDArgs *rda;
    char *args[2] = { NULL, NULL };

    rda = ReadArgs("PRINTER_IP/A", (LONG *)args, NULL);
    if (!rda) {
        PrintFault(IoErr(), "ippdump");
        return RETURN_FAIL;
    }

    char *ip = args[0];
    if (!ip) {
        printf("Missing IP address\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    printf("Got IP: %s\n", ip);

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        printf("Failed to open bsdsocket.library\n");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("Socket creation failed\n");
        CloseLibrary(SocketBase);
        return 1;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address\n");
        CloseSocket(sockfd);
        CloseLibrary(SocketBase);
        return 1;
    }

    printf("Connecting to %s...\n", ip);
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect\n");
        CloseSocket(sockfd);
        CloseLibrary(SocketBase);
        return 1;
    }

    // Construct URI
    char uri[128];
    snprintf(uri, sizeof(uri), "ipp://%s/ipp/print", ip);
    int uri_len = strlen(uri);

    // IPP payload
    unsigned char ipp_payload[512];
    int offset = 0;

    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01;  // IPP version 1.1
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B;  // Get-Printer-Attributes
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01;  // Request ID

    ipp_payload[offset++] = 0x01;  // operation-attributes-tag

    // attributes-charset
    ipp_payload[offset++] = 0x47;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language
    ipp_payload[offset++] = 0x48;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1B;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri
    ipp_payload[offset++] = 0x45;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    // requested-attributes = all
    ipp_payload[offset++] = 0x44;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x14;
    memcpy(&ipp_payload[offset], "requested-attributes", 20); offset += 20;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x03;
    memcpy(&ipp_payload[offset], "all", 3); offset += 3;

    ipp_payload[offset++] = 0x03;  // end-of-attributes

    // HTTP header
    char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp/print HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        ip, offset);

    send(sockfd, http_header, strlen(http_header), 0);
    send(sockfd, (char*)ipp_payload, offset, 0);

    char response[MAX_RESPONSE];
    int total_received = 0, received;

    while ((received = recv(sockfd, response + total_received, MAX_RESPONSE - total_received, 0)) > 0) {
        total_received += received;
    }

    response[total_received] = '\0';
    printf("\n=== HTTP HEADER ===\n");
    char *header_end = strstr(response, "\r\n\r\n");
    if (!header_end) {
        printf("No HTTP header end found!\n");
        CloseSocket(sockfd);
        CloseLibrary(SocketBase);
        return 1;
    }

    *header_end = '\0';
    puts(response);
    *header_end = '\r';  // restore (not really needed now)

    unsigned char *ipp_data = (unsigned char *)(header_end + 4);
    int ipp_len = total_received - (ipp_data - (unsigned char *)response);

    printf("\n=== IPP PAYLOAD (%d bytes) ===\n", ipp_len);
    for (int i = 0; i < ipp_len; i++) {
        printf("%02X ", ipp_data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    parse_ipp_attributes(ipp_data, ipp_len);
    parse_media_col_ready(ipp_data, ipp_len);
// Save the raw IPP payload to a file
BPTR fh = Open("ipp_response.bin", MODE_NEWFILE);
if (fh) {
    Write(fh, ipp_data, ipp_len);
    Close(fh);
    printf("Saved IPP payload to ipp_response.bin\n");
} else {
    printf("Failed to save IPP payload to file\n");
}
    CloseSocket(sockfd);
    CloseLibrary(SocketBase);
    return 0;
}
