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

#define PORT 631
#define MAX_BUFFER 128000
#define MAX_OUTPUT_LINES 10
#define MAX_OUTPUT_LINE_LENGTH 80

// Gadget IDs
#define GAD_IP_STRING 1
#define GAD_FILE_STRING 2
#define GAD_QUERY_BUTTON 3
#define GAD_PRINT_BUTTON 4
#define GAD_EXIT_BUTTON 5

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
int output_line = 0;
struct Screen *screen = NULL;
void *vi = NULL;
struct TextFont *font = NULL;

// Font definition
struct TextAttr Topaz80 = {
    "topaz.font",
    8,
    0,
    0
};

// Redirect printf to buffer
void custom_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char temp[256];
    vsnprintf(temp, sizeof(temp), format, args);
    va_end(args);

    // Shift lines up if buffer is full
    if (output_line >= MAX_OUTPUT_LINES) {
        for (int i = 0; i < MAX_OUTPUT_LINES - 1; i++) {
            strncpy(output_buffer[i], output_buffer[i + 1], MAX_OUTPUT_LINE_LENGTH);
        }
        output_line = MAX_OUTPUT_LINES - 1;
    }

    // Add new line
    strncpy(output_buffer[output_line], temp, MAX_OUTPUT_LINE_LENGTH - 1);
    output_buffer[output_line][MAX_OUTPUT_LINE_LENGTH - 1] = '\0';
    output_line++;

    // Remove trailing newline for display
    int len = strlen(output_buffer[output_line - 1]);
    if (len > 0 && output_buffer[output_line - 1][len - 1] == '\n') {
        output_buffer[output_line - 1][len - 1] = '\0';
    }

    // Refresh window
    if (window) {
        struct RastPort *rp = window->RPort;
        SetAPen(rp, 1);
        SetBPen(rp, 0);
        SetDrMd(rp, JAM2);

        // Clear the output area
        RectFill(rp, 10, 100, window->Width - 10, window->Height - 10);

        // Draw the output lines
        for (int i = 0; i < output_line; i++) {
            Move(rp, 10, 110 + i * 10);
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
    struct sockaddr_in serv_addr;
    int sockfd = -1;
    static unsigned char ipp_payload[2048];
    int offset = 0;
    FILE *log_file = fopen("ipp_log.txt", "w");
    if (!log_file) {
        printf("Failed to open log file for writing\n");
        return -1;
    }

    const char *uri = "ipp://192.168.0.44/ipp";
    int uri_len = strlen(uri);

    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B;
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

    const char *requested = "document-format-supported,media-supported,printer-state,operations-supported";
    int requested_len = strlen(requested);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "requested-attributes", 18); offset += 18;
    ipp_payload[offset++] = (requested_len >> 8) & 0xFF;
    ipp_payload[offset++] = requested_len & 0xFF;
    memcpy(&ipp_payload[offset], requested, requested_len); offset += requested_len;

    ipp_payload[offset++] = 0x03;

    static char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        ip, offset);

    printf("Querying printer attributes at %s...\n", ip);
    fprintf(log_file, "Querying printer attributes at %s...\n", ip);
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        strncpy(response, "Socket creation failed.", maxlen);
        fclose(log_file);
        return -1;
    }
    printf("Socket created: %d\n", sockfd);
    fprintf(log_file, "Socket created: %d\n", sockfd);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        fprintf(log_file, "Failed to set socket timeout\n");
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }
    printf("Socket timeout set\n");
    fprintf(log_file, "Socket timeout set\n");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address: %s\n", ip);
        fprintf(log_file, "Invalid IP address: %s\n", ip);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }
    printf("Server address prepared: %s:%d\n", ip, PORT);
    fprintf(log_file, "Server address prepared: %s:%d\n", ip, PORT);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        strncpy(response, "Failed to connect to printer.", maxlen);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }
    printf("Connected!\n");
    fprintf(log_file, "Connected!\n");

    printf("Sending HTTP header...\n");
    fprintf(log_file, "Sending HTTP header...\n");
    if (send(sockfd, http_header, strlen(http_header), 0) < 0) {
        strncpy(response, "Failed sending header.", maxlen);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }
    printf("Sending IPP payload...\n");
    fprintf(log_file, "Sending IPP payload...\n");
    if (send(sockfd, (char *)ipp_payload, offset, 0) < 0) {
        strncpy(response, "Failed sending IPP.", maxlen);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }

    printf("Waiting for response...\n");
    fprintf(log_file, "Waiting for response...\n");
    ssize_t received = recv(sockfd, response, maxlen - 1, 0);
    if (received <= 0) {
        strncpy(response, "No response or receive timeout.", maxlen);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }

    response[received] = '\0';
    printf("Received %d bytes\n", (int)received);
    fprintf(log_file, "Received %d bytes\n", (int)received);

    char *ipp_start = strstr(response, "\r\n\r\n");
    if (!ipp_start) {
        printf("Could not find IPP response payload.\n");
        fprintf(log_file, "Could not find IPP response payload.\n");
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }

    ipp_start += 4;
    int ipp_len = received - (ipp_start - response);
    if (ipp_len < 4) {
        printf("IPP response too short to parse (%d bytes).\n", ipp_len);
        fprintf(log_file, "IPP response too short to parse (%d bytes).\n", ipp_len);
        CloseSocket(sockfd);
        fclose(log_file);
        return -1;
    }

    printf("IPP Version: %d.%d\n", ipp_start[0], ipp_start[1]);
    fprintf(log_file, "IPP Version: %d.%d\n", ipp_start[0], ipp_start[1]);
    printf("IPP Status Code: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
    fprintf(log_file, "IPP Status Code: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);

    int pos = 8;
    char name[512];
    char value[512];
    int iteration_count = 0;
    const int max_iterations = 10000;

    while (pos < ipp_len) {
        if (iteration_count++ > max_iterations) {
            printf("Error: Possible infinite loop detected at pos %d, aborting\n", pos);
            fprintf(log_file, "Error: Possible infinite loop detected at pos %d, aborting\n", pos);
            break;
        }

        int last_pos = pos;
        unsigned char tag = ipp_start[pos];
        pos++;
        fprintf(log_file, "Parsing tag 0x%02x at pos %d\n", tag, pos - 1);

        if (tag == 0x03) {
            printf("End of attributes reached\n");
            fprintf(log_file, "End of attributes reached\n");
            break;
        } else if (tag >= 0x01 && tag <= 0x05) {
            while (pos < ipp_len) {
                if (iteration_count++ > max_iterations) {
                    printf("Error: Possible infinite loop in attribute group at pos %d, aborting\n", pos);
                    fprintf(log_file, "Error: Possible infinite loop in attribute group at pos %d, aborting\n", pos);
                    break;
                }

                if (ipp_start[pos] <= 0x05 || ipp_start[pos] == 0x03) {
                    fprintf(log_file, "End of attribute group at pos %d\n", pos);
                    break;
                }

                unsigned char value_tag = ipp_start[pos];
                pos++;
                fprintf(log_file, "Value tag 0x%02x at pos %d\n", value_tag, pos - 1);

                if (pos + 2 > ipp_len) {
                    printf("Invalid name length position at pos %d, aborting\n", pos);
                    fprintf(log_file, "Invalid name length position at pos %d, aborting\n", pos);
                    break;
                }
                int name_len = (ipp_start[pos] << 8) | ipp_start[pos + 1];
                pos += 2;
                fprintf(log_file, "Name length: %d at pos %d\n", name_len, pos - 2);

                memset(name, 0, sizeof(name));
                if (name_len > 0 && pos + name_len <= ipp_len) {
                    if (name_len >= sizeof(name)) {
                        printf("Warning: Name length %d exceeds buffer size, skipping attribute\n", name_len);
                        fprintf(log_file, "Warning: Name length %d exceeds buffer size, skipping attribute\n", name_len);
                        pos += name_len;
                        continue;
                    }
                    strncpy(name, ipp_start + pos, name_len);
                    name[name_len] = '\0';
                    pos += name_len;
                    fprintf(log_file, "Attribute name: %s\n", name);
                } else {
                    printf("Invalid name length %d or buffer overflow at pos %d, skipping\n", name_len, pos);
                    fprintf(log_file, "Invalid name length %d or buffer overflow at pos %d, skipping\n", name_len, pos);
                    pos += name_len;
                    continue;
                }

                if (pos + 2 > ipp_len) {
                    printf("Invalid value length position at pos %d, aborting\n", pos);
                    fprintf(log_file, "Invalid value length position at pos %d, aborting\n", pos);
                    break;
                }
                int value_len = (ipp_start[pos] << 8) | ipp_start[pos + 1];
                pos += 2;
                fprintf(log_file, "Value length: %d at pos %d\n", value_len, pos - 2);

                memset(value, 0, sizeof(value));
                if (value_len >= 0 && pos + value_len <= ipp_len) {
                    if (value_len == 0) {
                        fprintf(log_file, "Value length is 0 for attribute %s, skipping value\n", name);
                    } else if (value_len >= sizeof(value)) {
                        printf("Warning: Value length %d exceeds buffer size for attribute %s, skipping\n", value_len, name);
                        fprintf(log_file, "Warning: Value length %d exceeds buffer size for attribute %s, skipping\n", value_len, name);
                        pos += value_len;
                        continue;
                    } else if (value_tag == 0x34) {
                        fprintf(log_file, "Skipping collection attribute %s\n", name);
                        pos += value_len;
                    } else if (value_tag == 0x44 || value_tag == 0x49) {
                        strncpy(value, ipp_start + pos, value_len);
                        value[value_len] = '\0';
                        if (strcmp(name, "document-format-supported") == 0 ||
                            strcmp(name, "media-supported") == 0) {
                            printf("Attribute: %s = %s\n", name, value);
                        }
                        fprintf(log_file, "Attribute: %s = %s\n", name, value);
                    } else if (value_tag == 0x47 || value_tag == 0x48) {
                        strncpy(value, ipp_start + pos, value_len);
                        value[value_len] = '\0';
                        fprintf(log_file, "Attribute: %s = %s\n", name, value);
                    } else if (value_tag == 0x21) {
                        if (value_len == 4) {
                            int int_value = (ipp_start[pos] << 24) |
                                            (ipp_start[pos + 1] << 16) |
                                            (ipp_start[pos + 2] << 8) |
                                            ipp_start[pos + 3];
                            if (strcmp(name, "printer-state") == 0) {
                                printf("Attribute: %s = %d\n", name, int_value);
                            }
                            fprintf(log_file, "Attribute: %s = %d\n", name, int_value);
                        } else {
                            fprintf(log_file, "Invalid integer length %d for attribute %s, skipping\n", value_len, name);
                        }
                    } else if (value_tag == 0x23) {
                        if (value_len == 4) {
                            int enum_value = (ipp_start[pos] << 24) |
                                             (ipp_start[pos + 1] << 16) |
                                             (ipp_start[pos + 2] << 8) |
                                             ipp_start[pos + 3];
                            if (strcmp(name, "operations-supported") == 0) {
                                printf("Attribute: %s = 0x%04x\n", name, enum_value);
                            }
                            fprintf(log_file, "Attribute: %s = 0x%04x\n", name, enum_value);
                        } else {
                            fprintf(log_file, "Invalid enum length %d for attribute %s, skipping\n", value_len, name);
                        }
                    } else {
                        fprintf(log_file, "Unsupported value tag 0x%02x for attribute %s, skipping\n", value_tag, name);
                    }
                    pos += value_len;
                } else {
                    printf("Invalid value length %d or buffer overflow at pos %d for attribute %s, skipping\n", value_len, pos, name);
                    fprintf(log_file, "Invalid value length %d or buffer overflow at pos %d for attribute %s, skipping\n", value_len, pos, name);
                    pos += value_len;
                    continue;
                }

                while (pos < ipp_len && ipp_start[pos] >= 0x10 && ipp_start[pos] <= 0x7F) {
                    if (iteration_count++ > max_iterations) {
                        printf("Error: Possible infinite loop in additional values at pos %d, aborting\n", pos);
                        fprintf(log_file, "Error: Possible infinite loop in additional values at pos %d, aborting\n", pos);
                        break;
                    }

                    value_tag = ipp_start[pos];
                    pos++;
                    fprintf(log_file, "Additional value tag 0x%02x at pos %d\n", value_tag, pos - 1);

                    if (pos + 2 > ipp_len) {
                        printf("Invalid name length position for additional value at pos %d, aborting\n", pos);
                        fprintf(log_file, "Invalid name length position for additional value at pos %d, aborting\n", pos);
                        break;
                    }
                    int additional_name_len = (ipp_start[pos] << 8) | ipp_start[pos + 1];
                    pos += 2;
                    fprintf(log_file, "Additional name length: %d at pos %d\n", additional_name_len, pos - 2);

                    if (additional_name_len != 0) {
                        fprintf(log_file, "Not an additional value (name length %d), breaking to outer loop\n", additional_name_len);
                        pos -= 3;
                        break;
                    }

                    if (pos + 2 > ipp_len) {
                        printf("Invalid value length position for additional value at pos %d, aborting\n", pos);
                        fprintf(log_file, "Invalid value length position for additional value at pos %d, aborting\n", pos);
                        break;
                    }
                    value_len = (ipp_start[pos] << 8) | ipp_start[pos + 1];
                    pos += 2;
                    fprintf(log_file, "Additional value length: %d at pos %d\n", value_len, pos - 2);

                    if (value_len >= 0 && pos + value_len <= ipp_len) {
                        if (value_len == 0) {
                            fprintf(log_file, "Additional value length is 0 for attribute %s, skipping\n", name);
                        } else if (value_len >= sizeof(value)) {
                            printf("Warning: Additional value length %d exceeds buffer size for attribute %s, skipping\n", value_len, name);
                            fprintf(log_file, "Warning: Additional value length %d exceeds buffer size for attribute %s, skipping\n", value_len, name);
                            pos += value_len;
                            continue;
                        } else if (value_tag == 0x34) {
                            fprintf(log_file, "Skipping additional collection value for attribute %s\n", name);
                            pos += value_len;
                        } else if (value_tag == 0x44 || value_tag == 0x49) {
                            strncpy(value, ipp_start + pos, value_len);
                            value[value_len] = '\0';
                            if (strcmp(name, "document-format-supported") == 0 ||
                                strcmp(name, "media-supported") == 0) {
                                printf("Attribute: %s = %s\n", name, value);
                            }
                            fprintf(log_file, "Attribute: %s = %s\n", name, value);
                        } else if (value_tag == 0x47 || value_tag == 0x48) {
                            strncpy(value, ipp_start + pos, value_len);
                            value[value_len] = '\0';
                            fprintf(log_file, "Attribute: %s = %s\n", name, value);
                        } else if (value_tag == 0x21) {
                            if (value_len == 4) {
                                int int_value = (ipp_start[pos] << 24) |
                                                (ipp_start[pos + 1] << 16) |
                                                (ipp_start[pos + 2] << 8) |
                                                ipp_start[pos + 3];
                                if (strcmp(name, "printer-state") == 0) {
                                    printf("Attribute: %s = %d\n", name, int_value);
                                }
                                fprintf(log_file, "Attribute: %s = %d\n", name, int_value);
                            }
                        } else if (value_tag == 0x23) {
                            if (value_len == 4) {
                                int enum_value = (ipp_start[pos] << 24) |
                                                 (ipp_start[pos + 1] << 16) |
                                                 (ipp_start[pos + 2] << 8) |
                                                 ipp_start[pos + 3];
                                if (strcmp(name, "operations-supported") == 0) {
                                    printf("Attribute: %s = 0x%04x\n", name, enum_value);
                                }
                                fprintf(log_file, "Attribute: %s = 0x%04x\n", name, enum_value);
                            }
                        }
                        pos += value_len;
                    } else {
                        printf("Invalid additional value length %d or buffer overflow at pos %d for attribute %s, skipping\n", value_len, pos, name);
                        fprintf(log_file, "Invalid additional value length %d or buffer overflow at pos %d for attribute %s, skipping\n", value_len, pos, name);
                        pos += value_len;
                        continue;
                    }
                }
            }
        } else {
            printf("Unexpected tag 0x%02x at pos %d, skipping\n", tag, pos - 1);
            fprintf(log_file, "Unexpected tag 0x%02x at pos %d, skipping\n", tag, pos - 1);
        }

        if (pos == last_pos) {
            printf("Error: Parsing stuck at pos %d, aborting\n", pos);
            fprintf(log_file, "Error: Parsing stuck at pos %d, aborting\n", pos);
            break;
        }
    }

    printf("Finished parsing IPP response\n");
    fprintf(log_file, "Finished parsing IPP response\n");
    fclose(log_file);
    CloseSocket(sockfd);
    return 0;
}

int send_print_job(const char *ip, const char *filename) {
    struct sockaddr_in serv_addr;
    int sockfd = -1;
    static unsigned char ipp_payload[2048];
    int offset = 0;

    const char *uri = "ipp://192.168.0.44/ipp";
    int uri_len = strlen(uri);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open file: %s\n", filename);
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        printf("Failed to allocate memory for file data\n");
        fclose(file);
        return -1;
    }
    fread(file_data, 1, file_size, file);
    fclose(file);

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

    const char *doc_format = "image/jpeg";
    int doc_format_len = strlen(doc_format);
    ipp_payload[offset++] = 0x49; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0e;
    memcpy(&ipp_payload[offset], "document-format", 14); offset += 14;
    ipp_payload[offset++] = (doc_format_len >> 8) & 0xFF;
    ipp_payload[offset++] = doc_format_len & 0xFF;
    memcpy(&ipp_payload[offset], doc_format, doc_format_len); offset += doc_format_len;

    const char *media = "iso_a4_210x297mm";
    int media_len = strlen(media);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "media", 5); offset += 5;
    ipp_payload[offset++] = (media_len >> 8) & 0xFF;
    ipp_payload[offset++] = media_len & 0xFF;
    memcpy(&ipp_payload[offset], media, media_len); offset += media_len;

    ipp_payload[offset++] = 0x03;

    static char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        ip, offset + file_size);

    printf("Sending JPEG to printer at %s...\n", ip);
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        printf("Socket creation failed.\n");
        free(file_data);
        return -1;
    }
    printf("Socket created: %d\n", sockfd);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0) {
        printf("Failed to set socket timeout\n");
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }
    printf("Socket timeout set\n");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid IP address: %s\n", ip);
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }
    printf("Server address prepared: %s:%d\n", ip, PORT);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect to printer.\n");
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }
    printf("Connected!\n");

    printf("Sending HTTP header...\n");
    if (send(sockfd, http_header, strlen(http_header), 0) < 0) {
        printf("Failed sending header.\n");
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }
    printf("Sending IPP payload...\n");
    if (send(sockfd, (char *)ipp_payload, offset, 0) < 0) {
        printf("Failed sending IPP.\n");
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }
    printf("Sending JPEG data (%ld bytes)...\n", file_size);
    if (send(sockfd, file_data, file_size, 0) < 0) {
        printf("Failed sending JPEG data.\n");
        CloseSocket(sockfd);
        free(file_data);
        return -1;
    }

    free(file_data);

    printf("Waiting for response...\n");
    char response_buffer[4096];
    ssize_t received = recv(sockfd, response_buffer, sizeof(response_buffer) - 1, 0);
    if (received <= 0) {
        printf("No response or receive timeout.\n");
        CloseSocket(sockfd);
        return -1;
    }

    response_buffer[received] = '\0';
    char *ipp_start = strstr(response_buffer, "\r\n\r\n");
    if (ipp_start) {
        ipp_start += 4;
        printf("IPP Version: %d.%d\n", ipp_start[0], ipp_start[1]);
        printf("IPP Status Code: 0x%02x%02x\n", ipp_start[2], ipp_start[3]);
    } else {
        printf("Could not find IPP response payload.\n");
    }

    CloseSocket(sockfd);
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
    ng.ng_LeftEdge = 80;
    ng.ng_TopEdge = 20 + topborder;
    ng.ng_Width = 200;
    ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"_Printer IP:";
    ng.ng_GadgetID = GAD_IP_STRING;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)ip_buffer,
        GTST_MaxChars, sizeof(ip_buffer) - 1,
        GA_Immediate, TRUE,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create IP string gadget\n");
        return NULL;
    }

    // File path string gadget
    ng.ng_TopEdge += 20;
    ng.ng_GadgetText = (STRPTR)"_File Path:";
    ng.ng_GadgetID = GAD_FILE_STRING;
    gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String, (ULONG)file_buffer,
        GTST_MaxChars, sizeof(file_buffer) - 1,
        GA_Immediate, TRUE,
        GT_Underscore, '_',
        TAG_DONE);
    if (!gad) {
        printf("Failed to create file string gadget\n");
        return NULL;
    }

    // Query button
    ng.ng_LeftEdge = 10;
    ng.ng_TopEdge += 20;
    ng.ng_Width = 100;
    ng.ng_Height = 12;
    ng.ng_GadgetText = (STRPTR)"_Query Attributes";
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
    ng.ng_LeftEdge += 110;
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
    ng.ng_LeftEdge += 110;
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
                        case GAD_IP_STRING:
                            printf("IP string updated: %s\n", ip_buffer);
                            break;

                        case GAD_FILE_STRING:
                            printf("File string updated: %s\n", file_buffer);
                            break;

                        case GAD_QUERY_BUTTON:
                            printf("Querying printer attributes...\n");
                            if (query_printer_attributes(ip_buffer, response, sizeof(response)) == 0) {
                                printf("Printer Attributes Response:\n%s\n", response);
                            } else {
                                printf("Error querying attributes: %s\n", response);
                            }
                            break;

                        case GAD_PRINT_BUTTON:
                            printf("Sending file to printer at %s...\n", ip_buffer);
                            send_print_job(ip_buffer, file_buffer);
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
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) {
        printf("Failed to open intuition.library\n");
        return 1;
    }


    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
    if (!GfxBase) {
        printf("Failed to open intuition.library\n");
        return 1;
    }

    GadToolsBase = OpenLibrary("gadtools.library", 37);
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
    font = OpenFont(&Topaz80);
    if (!font) {
        printf("Failed to open Topaz 8 font\n");
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
        WA_InnerHeight, 140,
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
        return 1;
    }

    // Refresh window
    GT_RefreshWindow(window, NULL);

    // Process events
    process_window_events(window);

    // Cleanup
    CloseWindow(window);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, screen);
    CloseFont(font);
    CloseLibrary(SocketBase);
    CloseLibrary(GadToolsBase);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}