/*
 * Minimal IPP Print-Job transport for MintPRINT spike #3.
 *
 * Uses bsdsocket.library directly and submits an already-created JPEG with a
 * standards-shaped IPP/1.1 Print-Job request to /ipp/print.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
typedef long ssize_t;
#include <proto/bsdsocket.h>

#include "ipp_client.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
struct Library *SocketBase = NULL;

static ULONG mp_len(const char *s)
{
    ULONG n = 0;
    while (s && s[n]) ++n;
    return n;
}

static int mp_append(char *dst, ULONG cap, ULONG *pos, const char *src)
{
    ULONG i = 0;
    while (src && src[i]) {
        if (*pos + 1 >= cap) return 0;
        dst[(*pos)++] = src[i++];
    }
    dst[*pos] = 0;
    return 1;
}

static int mp_append_ulong(char *dst, ULONG cap, ULONG *pos, ULONG value)
{
    char tmp[16];
    ULONG n = 0;
    if (value == 0) return mp_append(dst, cap, pos, "0");
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        char one[2];
        one[0] = tmp[--n]; one[1] = 0;
        if (!mp_append(dst, cap, pos, one)) return 0;
    }
    return 1;
}

static int mp_safe_send(int sock, const UBYTE *buf, ULONG len)
{
    ULONG sent_total = 0;
    while (sent_total < len) {
        ULONG left = len - sent_total;
        LONG want = (LONG)(left > 4096UL ? 4096UL : left);
        LONG sent = send(sock, (char *)(buf + sent_total), want, 0);
        if (sent <= 0) return 0;
        sent_total += (ULONG)sent;
    }
    return 1;
}

static int mp_put8(UBYTE *p, ULONG cap, ULONG *off, UBYTE v)
{
    if (*off >= cap) return 0;
    p[(*off)++] = v;
    return 1;
}

static int mp_put16(UBYTE *p, ULONG cap, ULONG *off, UWORD v)
{
    return mp_put8(p, cap, off, (UBYTE)(v >> 8)) &&
           mp_put8(p, cap, off, (UBYTE)(v & 255));
}

static int mp_put32(UBYTE *p, ULONG cap, ULONG *off, ULONG v)
{
    return mp_put8(p, cap, off, (UBYTE)(v >> 24)) &&
           mp_put8(p, cap, off, (UBYTE)(v >> 16)) &&
           mp_put8(p, cap, off, (UBYTE)(v >> 8)) &&
           mp_put8(p, cap, off, (UBYTE)v);
}

static int mp_put_bytes(UBYTE *p, ULONG cap, ULONG *off,
                        const UBYTE *src, ULONG len)
{
    ULONG i;
    if (*off + len > cap) return 0;
    for (i = 0; i < len; ++i) p[(*off)++] = src[i];
    return 1;
}

static int mp_ipp_attr(UBYTE *p, ULONG cap, ULONG *off, UBYTE tag,
                       const char *name, const char *value)
{
    ULONG nl = mp_len(name), vl = mp_len(value);
    if (nl > 65535UL || vl > 65535UL) return 0;
    return mp_put8(p, cap, off, tag) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl) &&
           mp_put16(p, cap, off, (UWORD)vl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)value, vl);
}

static LONG mp_file_size(BPTR fh)
{
    LONG end;
    if (!fh) return -1;
    if (Seek(fh, 0, OFFSET_END) == -1) return -1;
    end = Seek(fh, 0, OFFSET_CURRENT);
    if (end < 0) return -1;
    if (Seek(fh, 0, OFFSET_BEGINNING) == -1) return -1;
    return end;
}

static LONG mp_parse_http_status(const UBYTE *buf, ULONG len)
{
    ULONG i = 0;
    while (i < len && buf[i] != ' ') ++i;
    if (i + 4 > len) return 0;
    return (LONG)((buf[i+1]-'0') * 100 + (buf[i+2]-'0') * 10 + (buf[i+3]-'0'));
}

static LONG mp_find_body(const UBYTE *buf, ULONG len)
{
    ULONG i;
    if (len < 4) return -1;
    for (i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
            return (LONG)(i + 4);
    }
    return -1;
}

LONG mp_ipp_print_jpeg(CONST_STRPTR filename, struct MPIPPResult *result)
{
    BPTR fh = 0;
    LONG fsize;
    int sock = -1;
    struct sockaddr_in addr = {0};
    static UBYTE ipp[512];
    ULONG io = 0;
    static char uri[128];
    ULONG up = 0;
    static char http[512];
    ULONG hp = 0;
    static UBYTE filebuf[8192];
    static UBYTE response[2048];
    ULONG response_used = 0;
    LONG body_pos = -1;
    LONG rc = -1;

    if (result) {
        result->error = -1;
        result->http_status = 0;
        result->ipp_status = 0xffff;
        result->document_bytes = 0;
    }
    if (!DOSBase || !filename) return -1;

    fh = Open(filename, MODE_OLDFILE);
    if (!fh) { rc = -2; goto done; }
    fsize = mp_file_size(fh);
    if (fsize <= 0) { rc = -3; goto done; }
    if (result) result->document_bytes = (ULONG)fsize;

    uri[0] = 0;
    if (!mp_append(uri, sizeof(uri), &up, "ipp://") ||
        !mp_append(uri, sizeof(uri), &up, MP_IPP_HOST) ||
        !mp_append(uri, sizeof(uri), &up, MP_IPP_PATH)) {
        rc = -4; goto done;
    }

    /* IPP/1.1, Print-Job (0x0002), request-id 1. */
    if (!mp_put8(ipp, sizeof(ipp), &io, 1) ||
        !mp_put8(ipp, sizeof(ipp), &io, 1) ||
        !mp_put16(ipp, sizeof(ipp), &io, 0x0002) ||
        !mp_put32(ipp, sizeof(ipp), &io, 1) ||
        !mp_put8(ipp, sizeof(ipp), &io, 0x01) ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x47, "attributes-charset", "utf-8") ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x48, "attributes-natural-language", "en") ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x45, "printer-uri", uri) ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x42, "requesting-user-name", "Amiga") ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x42, "job-name", "MintPRINT AmigaOS") ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x49, "document-format", "image/jpeg") ||
        !mp_put8(ipp, sizeof(ipp), &io, 0x03)) {
        rc = -5; goto done;
    }

    http[0] = 0;
    if (!mp_append(http, sizeof(http), &hp, "POST " MP_IPP_PATH " HTTP/1.1\r\nHost: ") ||
        !mp_append(http, sizeof(http), &hp, MP_IPP_HOST) ||
        !mp_append(http, sizeof(http), &hp, ":") ||
        !mp_append_ulong(http, sizeof(http), &hp, MP_IPP_PORT) ||
        !mp_append(http, sizeof(http), &hp, "\r\nContent-Type: application/ipp\r\nContent-Length: ") ||
        !mp_append_ulong(http, sizeof(http), &hp, io + (ULONG)fsize) ||
        !mp_append(http, sizeof(http), &hp, "\r\nConnection: close\r\n\r\n")) {
        rc = -6; goto done;
    }

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) { rc = -7; goto done; }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { rc = -8; goto done; }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(MP_IPP_PORT);
    addr.sin_addr.s_addr = inet_addr((STRPTR)MP_IPP_HOST);
    if (addr.sin_addr.s_addr == INADDR_NONE) { rc = -9; goto done; }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { rc = -10; goto done; }

    if (!mp_safe_send(sock, (const UBYTE *)http, hp) ||
        !mp_safe_send(sock, ipp, io)) { rc = -11; goto done; }

    for (;;) {
        LONG got = Read(fh, filebuf, sizeof(filebuf));
        if (got < 0) { rc = -12; goto done; }
        if (got == 0) break;
        if (!mp_safe_send(sock, filebuf, (ULONG)got)) { rc = -13; goto done; }
    }

    while (response_used < sizeof(response)) {
        LONG got = recv(sock, (char *)(response + response_used),
                        (LONG)(sizeof(response) - response_used), 0);
        if (got <= 0) break;
        response_used += (ULONG)got;
        body_pos = mp_find_body(response, response_used);
        if (body_pos >= 0 && response_used >= (ULONG)body_pos + 8UL) break;
    }

    if (result) result->http_status = mp_parse_http_status(response, response_used);
    if (body_pos < 0) body_pos = mp_find_body(response, response_used);
    if (body_pos < 0 || response_used < (ULONG)body_pos + 4UL) { rc = -14; goto done; }

    if (result) {
        result->ipp_status = (UWORD)(((UWORD)response[body_pos+2] << 8) |
                                     (UWORD)response[body_pos+3]);
    }

    if (result && result->http_status != 200) { rc = -15; goto done; }
    if (result && result->ipp_status >= 0x0100) { rc = -16; goto done; }

    rc = 0;

done:
    if (sock >= 0) CloseSocket(sock);
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    if (fh) Close(fh);
    if (result) result->error = rc;
    return rc;
}
