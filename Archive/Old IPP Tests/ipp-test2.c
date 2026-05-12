/* Amiga IPP Print-Job Prototype (Query Printer Attributes)
   Sends a PDF file directly to an IPP printer (AirPrint-compatible)
   Compile with: m68k-amigaos-gcc -o IPP-Print main.c -lamiga -lm
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
typedef long ssize_t;
#include <proto/bsdsocket.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <string.h>
#include <stdio.h>

#define PORT 631
#define MAX_BUFFER 128000

struct Library *SocketBase;

int load_pdf(const char *path, unsigned char *buffer, int maxlen) {
    BPTR file = Open(path, MODE_OLDFILE);
    if (!file) return -1;

    LONG len = Read(file, buffer, maxlen);
    Close(file);

    if (len <= 0 || len >= maxlen) return -1;
    return len;
}

#include <stdio.h>

// Write RGB data to PWG Raster
int rgb_to_pwg(const char *filename, unsigned char *rgb_data, int width, int height) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("Failed to open PWG file: %s\n", filename);
        return -1;
    }

    // PWG Raster header (simplified)
    char header[128] = {0};
    memcpy(header, "RaS2", 4); // PWG Raster signature
    header[4] = 0x00; // MediaClass (default)
    header[8] = 0x00; // MediaColor (default)
    header[12] = 0x00; // MediaType (default)
    header[16] = 0x00; // PrintContentOptimize (default)
    header[20] = 0x00; // PrintQuality (default)
    header[24] = (width >> 24) & 0xFF; // CupsWidth
    header[25] = (width >> 16) & 0xFF;
    header[26] = (width >> 8) & 0xFF;
    header[27] = width & 0xFF;
    header[28] = (height >> 24) & 0xFF; // CupsHeight
    header[29] = (height >> 16) & 0xFF;
    header[30] = (height >> 8) & 0xFF;
    header[31] = height & 0xFF;
    header[32] = 8; // BitsPerColor (8 bits per channel)
    header[36] = 3; // ColorSpace (RGB)
    header[40] = 3; // NumColors (3 for RGB)
    header[44] = (width * 3 >> 24) & 0xFF; // CupsBytesPerLine
    header[45] = (width * 3 >> 16) & 0xFF;
    header[46] = (width * 3 >> 8) & 0xFF;
    header[47] = (width * 3) & 0xFF;
    fwrite(header, 1, 128, file);

    // Write RGB data
    fwrite(rgb_data, 1, width * height * 3, file);

    fclose(file);
    return 0;
}

int query_printer_attributes(const char *ip, char *response, int maxlen) {
    struct sockaddr_in serv_addr;
    int sockfd = -1;
    static unsigned char ipp_payload[2048];
    int offset = 0;
    FILE *log_file = fopen("ipp_log.txt", "w"); // Open a log file for full output
    if (!log_file) {
        printf("Failed to open log file for writing\n");
        return -1;
    }

    const char *uri = "ipp://192.168.0.44/ipp";
    int uri_len = strlen(uri);

    // IPP Header for Get-Printer-Attributes
    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01; // IPP 1.1
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B; // Get-Printer-Attributes
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01;

    ipp_payload[offset++] = 0x01; // operation attributes tag

    // attributes-charset
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language
    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri
    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    // requested-attributes
    const char *requested = "document-format-supported,media-supported,printer-state,operations-supported";
    int requested_len = strlen(requested);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "requested-attributes", 18); offset += 18;
    ipp_payload[offset++] = (requested_len >> 8) & 0xFF;
    ipp_payload[offset++] = requested_len & 0xFF;
    memcpy(&ipp_payload[offset], requested, requested_len); offset += requested_len;

    ipp_payload[offset++] = 0x03; // end of attributes

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

    // Parse the IPP response for supported attributes
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
                    } else if (value_tag == 0x34) { // collection
                        fprintf(log_file, "Skipping collection attribute %s\n", name);
                        pos += value_len;
                    } else if (value_tag == 0x44 || value_tag == 0x49) { // keyword or mimeMediaType
                        strncpy(value, ipp_start + pos, value_len);
                        value[value_len] = '\0';
                        if (strcmp(name, "document-format-supported") == 0 ||
                            strcmp(name, "media-supported") == 0) {
                            printf("Attribute: %s = %s\n", name, value);
                        }
                        fprintf(log_file, "Attribute: %s = %s\n", name, value);
                    } else if (value_tag == 0x47 || value_tag == 0x48) { // charset or natural-language
                        strncpy(value, ipp_start + pos, value_len);
                        value[value_len] = '\0';
                        fprintf(log_file, "Attribute: %s = %s\n", name, value);
                    } else if (value_tag == 0x21) { // integer
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
                    } else if (value_tag == 0x23) { // enum
                        if (value_len == 4) {
                            int enum_value = (ipp_start[pos] << 24) |
                                             (ipp_start[pos + 1] << 16) |
                                             (ipp_start[pos + 2] << 8) |
                                             (ipp_start[pos + 3]);
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

                // Handle additional values for multi-valued attributes
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
                        pos -= 3; // Rewind to the value tag of the new attribute
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
                                                 (ipp_start[pos + 3]);
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

    // Read the JPEG file
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

    // IPP Header for Print-Job
    ipp_payload[offset++] = 0x01; ipp_payload[offset++] = 0x01; // IPP 1.1
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02; // Print-Job
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01;

    ipp_payload[offset++] = 0x01; // operation attributes tag

    // attributes-charset
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language
    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri
    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    // job-name
    const char *job_name = "Amiga";
    int job_name_len = strlen(job_name);
    ipp_payload[offset++] = 0x42; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x08;
    memcpy(&ipp_payload[offset], "job-name", 8); offset += 8;
    ipp_payload[offset++] = (job_name_len >> 8) & 0xFF;
    ipp_payload[offset++] = job_name_len & 0xFF;
    memcpy(&ipp_payload[offset], job_name, job_name_len); offset += job_name_len;

    // document-format
    const char *doc_format = "image/jpeg";
    int doc_format_len = strlen(doc_format);
    ipp_payload[offset++] = 0x49; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0e;
    memcpy(&ipp_payload[offset], "document-format", 14); offset += 14;
    ipp_payload[offset++] = (doc_format_len >> 8) & 0xFF;
    ipp_payload[offset++] = doc_format_len & 0xFF;
    memcpy(&ipp_payload[offset], doc_format, doc_format_len); offset += doc_format_len;

    // media
    const char *media = "iso_a4_210x297mm";
    int media_len = strlen(media);
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "media", 5); offset += 5;
    ipp_payload[offset++] = (media_len >> 8) & 0xFF;
    ipp_payload[offset++] = media_len & 0xFF;
    memcpy(&ipp_payload[offset], media, media_len); offset += media_len;

    ipp_payload[offset++] = 0x03; // end of attributes

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

int main(void) {
    char printer_ip[64] = "192.168.0.44";
    char pdf_path[256] = "UHD:test.jpg";
    char response[MAX_BUFFER];

    SocketBase = OpenLibrary("bsdsocket.library", 0);
    if (!SocketBase) {
        printf("Failed to open bsdsocket.library\n");
        return 1;
    }

    // Query the printer's supported attributes
    printf("Querying printer attributes...\n");
    if (query_printer_attributes(printer_ip, response, sizeof(response)) == 0) {
        printf("Printer Attributes Response:\n%s\n", response);
    } else {
        printf("Error querying attributes: %s\n", response);
    }

    // Send the print job
    printf("Sending JPEG to printer at %s...\n", printer_ip);
    send_print_job("192.168.0.44", "UHD:test.jpg");

    CloseLibrary(SocketBase);
    return 0;
}