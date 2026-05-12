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
#define MAX_RESPONSE 8192

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

        printf("Attr: %s = %s\n", name, value);
    }

    printf("--- Done decoding ---\n");
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
