/*
 * AmigaOS-compatible IPP logger
 * Queries hardcoded AirPrint-compatible printer (192.168.0.29)
 * and logs the raw IPP binary response to a file ("ipp_output.log")
 *
 * Compile with: m68k-amigaos-gcc -o ipp-test ipp_logger_amiga.c -lamiga -lsocket
 */

#include <proto/exec.h>
typedef long ssize_t;
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <proto/utility.h>
#include <proto/alib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 631

static const char *printer_ip = "192.168.0.29";

int main(void) {
    struct Library *SocketBase = OpenLibrary("bsdsocket.library", 0);
    if (!SocketBase) {
        PutStr("bsdsocket.library not available\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PutStr("Socket creation failed\n");
        CloseLibrary(SocketBase);
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)printer_ip);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        PutStr("Connection failed\n");
        CloseSocket(sock);
        CloseLibrary(SocketBase);
        return 1;
    }

    unsigned char ipp_payload[512];
    int offset = 0;
    const char *uri = "ipp://192.168.0.29/ipp";
    int uri_len = strlen(uri);

    ipp_payload[offset++] = 0x01; // IPP version major
    ipp_payload[offset++] = 0x01; // IPP version minor
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B; // Get-Printer-Attributes
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x01; // request-id

    // operation-attributes-tag
    ipp_payload[offset++] = 0x01; 
    // attributes-charset
    ipp_payload[offset++] = 0x47; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language
    ipp_payload[offset++] = 0x48; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x1B;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri
    ipp_payload[offset++] = 0x45; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x0B;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    // requested-attributes = all
    ipp_payload[offset++] = 0x44; ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x14;
    memcpy(&ipp_payload[offset], "requested-attributes", 20); offset += 20;
    ipp_payload[offset++] = 0x00; ipp_payload[offset++] = 0x03;
    memcpy(&ipp_payload[offset], "all", 3); offset += 3;

    // end-of-attributes-tag
    ipp_payload[offset++] = 0x03;

    // Build HTTP POST header
    char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        printer_ip, offset);

    send(sock, http_header, strlen(http_header), 0);
    send(sock, (char *)ipp_payload, offset, 0);

    unsigned char response[8192];
    int received = recv(sock, (char *)response, sizeof(response), 0);
    CloseSocket(sock);
    CloseLibrary(SocketBase);

    if (received <= 0) {
        PutStr("No response from printer\n");
        return 1;
    }

    BPTR fh = Open("ipp_output.log", MODE_NEWFILE);
    if (!fh) {
        PutStr("Failed to open ipp_output.log\n");
        return 1;
    }

    FPrintf(fh, "IPP Response (%ld bytes):\n", received);
    for (int i = 0; i < received; i += 16) {
        FPrintf(fh, "%04lx: ", i);
        for (int j = 0; j < 16 && (i + j) < received; j++) {
            FPrintf(fh, "%02lx ", response[i + j]);
        }
        FPrintf(fh, "\n");
    }

    Close(fh);
    PutStr("IPP response written to ipp_output.log\n");
    return 0;
}
