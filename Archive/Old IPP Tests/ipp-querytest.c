/*
   Amiga IPP Minimal Decoder
   Connects to hardcoded printer IP, sends Get-Printer-Attributes request,
   and decodes the binary IPP response into a readable log.

   Compile with:
   m68k-amigaos-gcc -o ipp-dump ipp-dump.c -lamiga -lsocket -lm
*/

#include <proto/exec.h>
typedef long ssize_t;  // Fix for Amiga GCC
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <proto/intuition.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>



#define PRINTER_IP "192.168.0.29"
#define PRINTER_PORT 631
#define BUFFER_SIZE 32768
FILE *log_file = NULL;
#define USED __attribute__((used))
static const char USED min_stack[] = "$STACK:65536";

struct Library *SocketBase = NULL;

void write_log(const char *format, ...) {
    if (!log_file) log_file = fopen("ipp_output.log", "w");
    if (!log_file) return;
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fflush(log_file);
}

void close_log() {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

int send_ipp_query(int sockfd) {
    unsigned char ipp_payload[512];
    int offset = 0;

    // IPP Header (version 1.1, operation 0x000B = Get-Printer-Attributes)
    ipp_payload[offset++] = 0x01; // major
    ipp_payload[offset++] = 0x01; // minor
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B; // operation-id
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01; // request-id

    // Operation Attributes Tag
    ipp_payload[offset++] = 0x01;

    // attributes-charset
    ipp_payload[offset++] = 0x47;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language
    ipp_payload[offset++] = 0x48;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1b;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri (ipp://192.168.0.29/ipp/print)
    const char *uri = "ipp://192.168.0.29/ipp/print";
    int uri_len = strlen(uri);
    ipp_payload[offset++] = 0x45;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0b;
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

    ipp_payload[offset++] = 0x03; // end of attributes

    char http_header[256];
    snprintf(http_header, sizeof(http_header),
             "POST /ipp/print HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/ipp\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n",
             PRINTER_IP, offset);

    send(sockfd, http_header, strlen(http_header), 0);
    send(sockfd, ipp_payload, offset, 0);
    return 0;
}

void parse_ipp_response(const unsigned char *data, int len) {
    int pos = 8;  // skip IPP header
    int name_len, value_len;
    char name[256], value[256];

    while (pos < len) {
        unsigned char tag = data[pos++];
        if (tag == 0x03) break;  // end-of-attributes-tag

        while (pos < len && data[pos] >= 0x10 && data[pos] <= 0xFF) {
            unsigned char value_tag = data[pos++];

            name_len = (data[pos] << 8) | data[pos + 1]; pos += 2;
            if (name_len > 255 || pos + name_len > len) return;
            memcpy(name, &data[pos], name_len); name[name_len] = 0; pos += name_len;

            value_len = (data[pos] << 8) | data[pos + 1]; pos += 2;
            if (value_len > 255 || pos + value_len > len) return;
            memcpy(value, &data[pos], value_len); value[value_len] = 0; pos += value_len;

            write_log("%s = %s\n", name, value);
        }
    }
}

int main(void) {
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) return 1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PRINTER_PORT);
    addr.sin_addr.s_addr = inet_addr(PRINTER_IP);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        write_log("Failed to connect to printer\n");
        return 1;
    }

    send_ipp_query(sockfd);

    unsigned char buffer[BUFFER_SIZE];
    int received = recv(sockfd, buffer, sizeof(buffer), 0);
    if (received > 0) {
        unsigned char *start = (unsigned char *)strstr((char *)buffer, "\r\n\r\n");
        if (start) {
            start += 4;
            int ipp_len = received - (start - buffer);
            parse_ipp_response(start, ipp_len);
        }
    }

    CloseSocket(sockfd);
    CloseLibrary(SocketBase);
    close_log();  // <== ADD THIS
    return 0;
}
