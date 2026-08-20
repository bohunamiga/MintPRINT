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

/* Parse the self-describing dimensions at the end of a PWG media name,
 * e.g. iso_a4_210x297mm or na_letter_8.5x11in.  IPP media-size uses
 * hundredths of a millimetre. */
static int mp_decimal_1000(const char *s, ULONG len, ULONG *value)
{
    ULONG whole = 0, frac = 0, frac_digits = 0, i;
    int dot = 0, any = 0;

    if (!s || !len || !value) return 0;
    for (i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '.' && !dot) { dot = 1; continue; }
        if (c < '0' || c > '9') return 0;
        any = 1;
        if (!dot) {
            if (whole > 100000UL) return 0;
            whole = whole * 10UL + (ULONG)(c - '0');
        } else if (frac_digits < 3) {
            frac = frac * 10UL + (ULONG)(c - '0');
            ++frac_digits;
        }
    }
    if (!any) return 0;
    while (frac_digits < 3) { frac *= 10UL; ++frac_digits; }
    *value = whole * 1000UL + frac;
    return 1;
}

static int mp_media_dimensions(const char *media, ULONG *x, ULONG *y)
{
    ULONG len, i, start = 0, sep = 0, sx, sy;
    ULONG factor;

    if (!media || !x || !y) return 0;
    len = mp_len(media);
    if (len < 6) return 0;

    for (i = 0; i < len; ++i) {
        if (media[i] == '_') start = i + 1;
    }
    for (i = start; i < len; ++i) {
        if (media[i] == 'x') { sep = i; break; }
    }
    if (!sep || sep <= start) return 0;

    if (len >= 2 && media[len - 2] == 'm' && media[len - 1] == 'm')
        factor = 100UL;       /* mm -> 1/100 mm */
    else if (len >= 2 && media[len - 2] == 'i' && media[len - 1] == 'n')
        factor = 2540UL;      /* inch -> 1/100 mm */
    else
        return 0;

    if (!mp_decimal_1000(media + start, sep - start, &sx) ||
        !mp_decimal_1000(media + sep + 1, (len - 2) - (sep + 1), &sy))
        return 0;

    *x = (sx * factor + 500UL) / 1000UL;
    *y = (sy * factor + 500UL) / 1000UL;
    return (*x && *y) ? 1 : 0;
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

/* start is the offset of the status line ("HTTP/1.1 NNN ...") to parse -
 * either 0 for the first response on the connection, or the byte just past
 * a previous response's header block when skipping an interim one (see
 * mp_find_body below). */
static LONG mp_parse_http_status(const UBYTE *buf, ULONG len, ULONG start)
{
    ULONG i = start;
    while (i < len && buf[i] != ' ') ++i;
    if (i + 4 > len) return 0;
    return (LONG)((buf[i+1]-'0') * 100 + (buf[i+2]-'0') * 10 + (buf[i+3]-'0'));
}

/* Finds the end of the next "\r\n\r\n" header terminator at or after start.
 * Some IPP servers (observed: a Canon TS8300) send an interim
 * "HTTP/1.1 100 Continue\r\n\r\n" ahead of the real status line and body;
 * the caller re-invokes this with start advanced past that block so the
 * 1xx response is skipped rather than mistaken for the final one. */
static LONG mp_find_body(const UBYTE *buf, ULONG len, ULONG start)
{
    ULONG i;
    if (len < 4 || start + 4 > len) return -1;
    for (i = start; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
            return (LONG)(i + 4);
    }
    return -1;
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
    if (!DOSBase || !cfg || !cfg->host[0] || cfg->port == 0 ||
        cfg->path[0] != '/' || !filename || !document_format) return -1;

    fh = Open(filename, MODE_OLDFILE);
    if (!fh) { rc = -2; goto done; }
    fsize = mp_file_size(fh);
    if (fsize <= 0) { rc = -3; goto done; }
    if (result) result->document_bytes = (ULONG)fsize;

    uri[0] = 0;
    if (!mp_append(uri, sizeof(uri), &up, "ipp://") ||
        !mp_append(uri, sizeof(uri), &up, cfg->host) ||
        !mp_append(uri, sizeof(uri), &up, cfg->path)) {
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
                            mp_media_dimensions(cfg->media, &media_x, &media_y);

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

    {
        ULONG header_start = 0;
        LONG status = 0;

        for (;;) {
            while (response_used < sizeof(response)) {
                LONG got;
                body_pos = mp_find_body(response, response_used, header_start);
                if (body_pos >= 0 && response_used >= (ULONG)body_pos + 8UL) break;
                got = recv(sock, (char *)(response + response_used),
                          (LONG)(sizeof(response) - response_used), 0);
                if (got <= 0) break;
                response_used += (ULONG)got;
            }
            body_pos = mp_find_body(response, response_used, header_start);
            if (body_pos < 0) { rc = -14; goto done; }

            status = mp_parse_http_status(response, response_used, header_start);
            if (status < 100 || status >= 200) break;

            /* Interim response (e.g. "100 Continue") - skip it and look
             * for the real status line/body that follows. */
            header_start = (ULONG)body_pos;
        }

        if (result) result->http_status = status;
    }
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
