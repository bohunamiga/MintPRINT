/* Simple Amiga IPP Test Print Prototype
   Compile with Amiga GCC, linking with -lamiga -lm
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
typedef long ssize_t;  // Needed for some socket headers
#include <proto/bsdsocket.h>
#include <intuition/intuition.h>
#include <string.h>
#include <stdio.h>

#define PORT 631
#define BUFFER_SIZE 2048



struct Library *SocketBase;

int send_test_ipp_request(const char *ip, char *response, int maxlen) {
    struct sockaddr_in serv_addr;
    int sockfd;

    // IPP URI to this printer
    const char *uri = "ipp://192.168.0.44/ipp/print";
    int uri_len = strlen(uri);

    // Create the IPP request payload
    unsigned char ipp_payload[512];
    int offset = 0;

    // IPP Header
    ipp_payload[offset++] = 0x01; // IPP version 1.1
    ipp_payload[offset++] = 0x01;
    ipp_payload[offset++] = 0x00; // Operation ID: Get-Printer-Attributes
    ipp_payload[offset++] = 0x0B;
    ipp_payload[offset++] = 0x00; // Request ID: 1
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x01;

    // Operation Attributes Tag
    ipp_payload[offset++] = 0x01;

    // attributes-charset (tag: 0x47)
    ipp_payload[offset++] = 0x47;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x12;
    memcpy(&ipp_payload[offset], "attributes-charset", 18); offset += 18;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x05;
    memcpy(&ipp_payload[offset], "utf-8", 5); offset += 5;

    // attributes-natural-language (tag: 0x48)
    ipp_payload[offset++] = 0x48;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x1B;
    memcpy(&ipp_payload[offset], "attributes-natural-language", 27); offset += 27;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x02;
    memcpy(&ipp_payload[offset], "en", 2); offset += 2;

    // printer-uri (tag: 0x45)
    ipp_payload[offset++] = 0x45;
    ipp_payload[offset++] = 0x00;
    ipp_payload[offset++] = 0x0B;
    memcpy(&ipp_payload[offset], "printer-uri", 11); offset += 11;
    ipp_payload[offset++] = (uri_len >> 8) & 0xFF;
    ipp_payload[offset++] = uri_len & 0xFF;
    memcpy(&ipp_payload[offset], uri, uri_len); offset += uri_len;

    // End-of-attributes tag
    ipp_payload[offset++] = 0x03;

    // Build HTTP header
    char http_header[256];
    snprintf(http_header, sizeof(http_header),
        "POST /ipp/print HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/ipp\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        ip, offset);

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        strcpy(response, "Failed to create socket.");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr((STRPTR)ip);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        strcpy(response, "Failed to connect to printer.");
        CloseSocket(sockfd);
        return -1;
    }

    send(sockfd, http_header, strlen(http_header), 0);
    send(sockfd, (char *)ipp_payload, offset, 0);

    int received = recv(sockfd, response, maxlen - 1, 0);
    if (received > 0) {
        response[received] = '\0';
    } else {
        strcpy(response, "No response or receive error.");
    }

    CloseSocket(sockfd);
    return 0;
}

int main(void) {
    struct Window *window;
    struct IntuiMessage *msg;
    BOOL running = TRUE;
    char printer_ip[64] = "192.168.0.44";
    char response[BUFFER_SIZE] = {0};

    SocketBase = OpenLibrary("bsdsocket.library", 0);
    if (!SocketBase) {
        printf("Failed to open bsdsocket.library\n");
        return 1;
    }

    window = OpenWindowTags(NULL,
        WA_Title, (ULONG)"IPP Test Print",
        WA_Width, 400,
        WA_Height, 120,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY,
        TAG_END);

    if (!window) {
        printf("Failed to open window\n");
        CloseLibrary(SocketBase);
        return 1;
    }

    printf("Enter Printer IP Address [%s]: ", printer_ip);
    fflush(stdout);
    fgets(printer_ip, sizeof(printer_ip), stdin);
    printer_ip[strcspn(printer_ip, "\n")] = '\0';

    if (send_test_ipp_request(printer_ip, response, BUFFER_SIZE) == 0) {
        printf("Printer Response:\n%s\n", response);
    } else {
        printf("Error: %s\n", response);
    }

    while (running) {
        WaitPort(window->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (msg->Class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            }
            ReplyMsg((struct Message *)msg);
        }
    }

    CloseWindow(window);
    CloseLibrary(SocketBase);
    return 0;
}
