/*
 * Minimal IPP Print-Job transport for MintPRINT working driver path.
 *
 * Uses bsdsocket.library directly and submits an already-created JPEG with a
 * standards-shaped IPP/1.1 Print-Job request to /ipp/print.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
/* ssize_t is provided by the AROS SDK; only define it manually for classic
 * m68k AmigaOS / libnix where it is absent from the system headers. */
#ifndef __AROS__
typedef long ssize_t;
#endif
#include <proto/bsdsocket.h>

#include "ipp_client.h"
#include "media_size.h"
#include "http_response.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
struct Library *SocketBase = NULL;

BOOL mp_ipp_socket_available(void)
{
    LONG probe_socket;

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) return FALSE;

    probe_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (probe_socket < 0) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return FALSE;
    }

    CloseSocket(probe_socket);
    CloseLibrary(SocketBase);
    SocketBase = NULL;
    return TRUE;
}

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

static int mp_ipp_enum_attr(UBYTE *p, ULONG cap, ULONG *off,
                            const char *name, ULONG value)
{
    ULONG nl = mp_len(name);
    if (nl > 65535UL) return 0;
    return mp_put8(p, cap, off, 0x23) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl) &&
           mp_put16(p, cap, off, 4) &&
           mp_put32(p, cap, off, value);
}

static int mp_member_name(UBYTE *p, ULONG cap, ULONG *off, const char *name)
{
    ULONG nl = mp_len(name);
    if (!nl || nl > 65535UL) return 0;
    return mp_put8(p, cap, off, 0x4a) &&
           mp_put16(p, cap, off, 0) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl);
}

static int mp_member_keyword(UBYTE *p, ULONG cap, ULONG *off,
                             const char *name, const char *value)
{
    ULONG vl = mp_len(value);
    if (vl > 65535UL) return 0;
    return mp_member_name(p, cap, off, name) &&
           mp_put8(p, cap, off, 0x44) &&
           mp_put16(p, cap, off, 0) &&
           mp_put16(p, cap, off, (UWORD)vl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)value, vl);
}

static int mp_member_integer(UBYTE *p, ULONG cap, ULONG *off,
                             const char *name, ULONG value)
{
    return mp_member_name(p, cap, off, name) &&
           mp_put8(p, cap, off, 0x21) &&
           mp_put16(p, cap, off, 0) &&
           mp_put16(p, cap, off, 4) &&
           mp_put32(p, cap, off, value);
}

static int mp_collection_begin(UBYTE *p, ULONG cap, ULONG *off,
                               const char *name)
{
    ULONG nl = mp_len(name);
    if (nl > 65535UL) return 0;
    return mp_put8(p, cap, off, 0x34) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl) &&
           mp_put16(p, cap, off, 0);
}

static int mp_nested_collection_begin(UBYTE *p, ULONG cap, ULONG *off)
{
    return mp_put8(p, cap, off, 0x34) &&
           mp_put16(p, cap, off, 0) &&
           mp_put16(p, cap, off, 0);
}

static int mp_collection_end(UBYTE *p, ULONG cap, ULONG *off)
{
    return mp_put8(p, cap, off, 0x37) &&
           mp_put16(p, cap, off, 0) &&
           mp_put16(p, cap, off, 0);
}

static int mp_media_col_attr(UBYTE *p, ULONG cap, ULONG *off,
                             ULONG x, ULONG y, const char *source)
{
    if (!mp_collection_begin(p, cap, off, "media-col")) return 0;
    if (!mp_member_name(p, cap, off, "media-size") ||
        !mp_nested_collection_begin(p, cap, off) ||
        !mp_member_integer(p, cap, off, "x-dimension", x) ||
        !mp_member_integer(p, cap, off, "y-dimension", y) ||
        !mp_collection_end(p, cap, off)) return 0;
    if (source && source[0] &&
        !mp_member_keyword(p, cap, off, "media-source", source)) return 0;
    return mp_collection_end(p, cap, off);
}

static ULONG mp_quality_enum(const char *quality)
{
    if (!quality || !quality[0]) return 0;
    if (quality[0] == '3' && quality[1] == 0) return 3;
    if (quality[0] == '4' && quality[1] == 0) return 4;
    if (quality[0] == '5' && quality[1] == 0) return 5;
    if (quality[0] == 'd' || quality[0] == 'D') return 3;
    if (quality[0] == 'n' || quality[0] == 'N') return 4;
    if (quality[0] == 'h' || quality[0] == 'H') return 5;
    return 0;
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

LONG mp_ipp_print_document(const struct MPConfig *cfg, CONST_STRPTR filename,
                           CONST_STRPTR document_format,
                           struct MPIPPResult *result)
{
    BPTR fh = 0;
    LONG fsize;
    int sock = -1;
    struct sockaddr_in addr = {0};
    static UBYTE ipp[1024];
    ULONG io = 0;
    static char uri[192];
    ULONG up = 0;
    static char http[512];
    ULONG hp = 0;
    static UBYTE filebuf[8192];
    static char response[8192];
    ULONG response_used = 0;
    int body_pos = -1;
    int body_len = 0;
    LONG rc = -1;

    if (result) {
        result->error = -1;
        result->http_status = 0;
        result->ipp_status = 0xffff;
        result->document_bytes = 0;
    }
    if (!DOSBase || !cfg || !cfg->host[0] || cfg->port == 0 ||
        cfg->path[0] != '/' || !filename || !document_format) return -1;

    fh = Open(filename, MODE_OLDFILE);
    if (!fh) { rc = -2; goto done; }
    fsize = mp_file_size(fh);
    if (fsize <= 0) { rc = -3; goto done; }
    if (result) result->document_bytes = (ULONG)fsize;

    /* 631 is the default/implied port for the ipp:// scheme and is safe to
     * omit; any other port must be stated explicitly, or this URI silently
     * claims a printer on 631 while the connection below actually goes
     * elsewhere (the Host: header and the real connect() already get the
     * port right - only this attribute value was missing it). */
    uri[0] = 0;
    if (!mp_append(uri, sizeof(uri), &up, "ipp://") ||
        !mp_append(uri, sizeof(uri), &up, cfg->host)) {
        rc = -4; goto done;
    }
    if (cfg->port != 631 &&
        (!mp_append(uri, sizeof(uri), &up, ":") ||
         !mp_append_ulong(uri, sizeof(uri), &up, cfg->port))) {
        rc = -4; goto done;
    }
    if (!mp_append(uri, sizeof(uri), &up, cfg->path)) {
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
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x49, "document-format", (const char *)document_format)) {
        rc = -5; goto done;
    }

    /* Optional Unit0 job-template attributes. Empty values preserve the
     * already-proven minimal Print-Job path.  A tray/source choice is
     * encoded correctly inside media-col rather than as a top-level
     * media-source attribute. */
    if (cfg->media[0] || cfg->source[0] || cfg->color[0] || cfg->quality[0] ||
        cfg->scaling[0] || cfg->sides[0]) {
        ULONG quality_enum = mp_quality_enum(cfg->quality);
        ULONG media_x = 0, media_y = 0;
        int use_media_col = cfg->media[0] && cfg->source[0] &&
                            mp_media_dimensions_100mm(cfg->media,
                                                      &media_x, &media_y);

        if (!mp_put8(ipp, sizeof(ipp), &io, 0x02)) { rc = -5; goto done; }
        if (use_media_col) {
            if (!mp_media_col_attr(ipp, sizeof(ipp), &io, media_x, media_y,
                                   cfg->source))
                { rc = -5; goto done; }
        } else if (cfg->media[0] &&
                   !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x44, "media", cfg->media))
            { rc = -5; goto done; }
        if (cfg->color[0] &&
            !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x44, "print-color-mode", cfg->color))
            { rc = -5; goto done; }
        if (quality_enum &&
            !mp_ipp_enum_attr(ipp, sizeof(ipp), &io, "print-quality", quality_enum))
            { rc = -5; goto done; }
        if (cfg->scaling[0] &&
            !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x44, "print-scaling", cfg->scaling))
            { rc = -5; goto done; }
        if (cfg->sides[0] &&
            !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x44, "sides", cfg->sides))
            { rc = -5; goto done; }
    }

    if (!mp_put8(ipp, sizeof(ipp), &io, 0x03)) {
        rc = -5; goto done;
    }

    http[0] = 0;
    if (!mp_append(http, sizeof(http), &hp, "POST ") ||
        !mp_append(http, sizeof(http), &hp, cfg->path) ||
        !mp_append(http, sizeof(http), &hp, " HTTP/1.1\r\nHost: ") ||
        !mp_append(http, sizeof(http), &hp, cfg->host) ||
        !mp_append(http, sizeof(http), &hp, ":") ||
        !mp_append_ulong(http, sizeof(http), &hp, cfg->port) ||
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
    addr.sin_port = htons(cfg->port);
    addr.sin_addr.s_addr = inet_addr((STRPTR)cfg->host);
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

    for (;;) {
        int parsed;
        int status = 0;
        LONG got;

        parsed = mp_http_final_body(response, (int)response_used, &status,
                                    &body_pos, &body_len);
        if (parsed == 1) {
            if (result) result->http_status = status;
            break;
        }
        if (parsed < 0 || response_used >= sizeof(response)) {
            rc = -14;
            goto done;
        }
        got = recv(sock, response + response_used,
                   (LONG)(sizeof(response) - response_used), 0);
        if (got <= 0) { rc = -14; goto done; }
        response_used += (ULONG)got;
    }
    if (body_pos < 0 || body_len < 8) { rc = -14; goto done; }

    if (result) {
        const UBYTE *ipp_response = (const UBYTE *)(response + body_pos);
        result->ipp_status = (UWORD)(((UWORD)ipp_response[2] << 8) |
                                     (UWORD)ipp_response[3]);
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
