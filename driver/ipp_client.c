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
#include "media_size.h"

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

static int mp_ipp_integer_attr(UBYTE *p, ULONG cap, ULONG *off,
                               const char *name, ULONG value)
{
    ULONG nl = mp_len(name);
    if (nl > 65535UL) return 0;
    return mp_put8(p, cap, off, 0x21) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl) &&
           mp_put16(p, cap, off, 4) &&
           mp_put32(p, cap, off, value);
}

static int mp_ipp_boolean_attr(UBYTE *p, ULONG cap, ULONG *off,
                               const char *name, BOOL value)
{
    ULONG nl = mp_len(name);
    if (nl > 65535UL) return 0;
    return mp_put8(p, cap, off, 0x22) &&
           mp_put16(p, cap, off, (UWORD)nl) &&
           mp_put_bytes(p, cap, off, (const UBYTE *)name, nl) &&
           mp_put16(p, cap, off, 1) &&
           mp_put8(p, cap, off, value ? 1 : 0);
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

static void mp_ipp_result_init(struct MPIPPResult *result)
{
    if (!result) return;
    result->error = -1;
    result->http_status = 0;
    result->ipp_status = 0xffff;
    result->document_bytes = 0;
    result->job_id = 0;
}

static int mp_build_printer_uri(const struct MPConfig *cfg,
                                char *uri, ULONG cap)
{
    ULONG pos = 0;

    uri[0] = 0;
    if (!mp_append(uri, cap, &pos, "ipp://") ||
        !mp_append(uri, cap, &pos, cfg->host)) return 0;
    if (cfg->port != 631 &&
        (!mp_append(uri, cap, &pos, ":") ||
         !mp_append_ulong(uri, cap, &pos, cfg->port))) return 0;
    return mp_append(uri, cap, &pos, cfg->path);
}

static int mp_ipp_begin_request(UBYTE *ipp, ULONG cap, ULONG *io,
                                UWORD operation, const char *uri,
                                ULONG job_id, BOOL include_job_name)
{
    return mp_put8(ipp, cap, io, 1) &&
           mp_put8(ipp, cap, io, 1) &&
           mp_put16(ipp, cap, io, operation) &&
           mp_put32(ipp, cap, io, 1) &&
           mp_put8(ipp, cap, io, 0x01) &&
           mp_ipp_attr(ipp, cap, io, 0x47,
                       "attributes-charset", "utf-8") &&
           mp_ipp_attr(ipp, cap, io, 0x48,
                       "attributes-natural-language", "en") &&
           mp_ipp_attr(ipp, cap, io, 0x45, "printer-uri", uri) &&
           (!job_id ||
            mp_ipp_integer_attr(ipp, cap, io, "job-id", job_id)) &&
           mp_ipp_attr(ipp, cap, io, 0x42,
                       "requesting-user-name", "Amiga") &&
           (!include_job_name ||
            mp_ipp_attr(ipp, cap, io, 0x42,
                        "job-name", "MintPRINT AmigaOS"));
}

static int mp_ipp_job_template(UBYTE *ipp, ULONG cap, ULONG *io,
                               const struct MPConfig *cfg,
                               BOOL multi_document)
{
    ULONG quality_enum = mp_quality_enum(cfg->quality);
    ULONG media_x = 0, media_y = 0;
    int use_media_col = cfg->media[0] && cfg->source[0] &&
                        mp_media_dimensions_100mm(cfg->media,
                                                  &media_x, &media_y);

    if (!(cfg->media[0] || cfg->source[0] || cfg->color[0] ||
          cfg->quality[0] || cfg->scaling[0] || cfg->sides[0] ||
          multi_document)) return 1;

    if (!mp_put8(ipp, cap, io, 0x02)) return 0;
    if (use_media_col) {
        if (!mp_media_col_attr(ipp, cap, io, media_x, media_y,
                               cfg->source)) return 0;
    } else if (cfg->media[0] &&
               !mp_ipp_attr(ipp, cap, io, 0x44, "media", cfg->media)) {
        return 0;
    }
    if (cfg->color[0] &&
        !mp_ipp_attr(ipp, cap, io, 0x44,
                     "print-color-mode", cfg->color)) return 0;
    if (quality_enum &&
        !mp_ipp_enum_attr(ipp, cap, io,
                          "print-quality", quality_enum)) return 0;
    if (cfg->scaling[0] &&
        !mp_ipp_attr(ipp, cap, io, 0x44,
                     "print-scaling", cfg->scaling)) return 0;
    if (cfg->sides[0] &&
        !mp_ipp_attr(ipp, cap, io, 0x44,
                     "sides", cfg->sides)) return 0;
    if (multi_document &&
        !mp_ipp_attr(ipp, cap, io, 0x44,
                     "multiple-document-handling",
                     "single-document")) return 0;
    return 1;
}

static int mp_ipp_find_job_id(const UBYTE *body, ULONG length,
                              ULONG *job_id)
{
    ULONG pos = 8;

    if (!body || length < 8 || !job_id) return 0;
    while (pos < length) {
        UBYTE tag = body[pos++];
        UWORD name_len;
        UWORD value_len;
        const UBYTE *name;
        const UBYTE *value;

        if (tag == 0x03) break;
        if (tag <= 0x0f) continue;
        if (pos + 2 > length) return 0;
        name_len = (UWORD)(((UWORD)body[pos] << 8) | body[pos + 1]);
        pos += 2;
        if (pos + name_len + 2 > length) return 0;
        name = body + pos;
        pos += name_len;
        value_len = (UWORD)(((UWORD)body[pos] << 8) | body[pos + 1]);
        pos += 2;
        if (pos + value_len > length) return 0;
        value = body + pos;
        pos += value_len;

        if (tag == 0x21 && name_len == 6 && value_len == 4 &&
            name[0] == 'j' && name[1] == 'o' && name[2] == 'b' &&
            name[3] == '-' && name[4] == 'i' && name[5] == 'd') {
            *job_id = ((ULONG)value[0] << 24) |
                      ((ULONG)value[1] << 16) |
                      ((ULONG)value[2] << 8) |
                      (ULONG)value[3];
            return *job_id != 0;
        }
    }
    return 0;
}

static LONG mp_final_http_body(const UBYTE *response, ULONG used,
                               LONG *http_status)
{
    ULONG header_start = 0;

    for (;;) {
        LONG body = mp_find_body(response, used, header_start);
        LONG status;
        if (body < 0) return -1;
        status = mp_parse_http_status(response, used, header_start);
        if (status < 100 || status >= 200) {
            if (http_status) *http_status = status;
            return body;
        }
        header_start = (ULONG)body;
    }
}

static LONG mp_ipp_exchange(const struct MPConfig *cfg,
                            const UBYTE *ipp, ULONG ipp_size,
                            BPTR fh, ULONG file_size,
                            BOOL need_job_id,
                            struct MPIPPResult *result)
{
    int sock = -1;
    struct sockaddr_in addr = {0};
    static char http[512];
    ULONG hp = 0;
    static UBYTE filebuf[8192];
    static UBYTE response[2048];
    ULONG response_used = 0;
    LONG body_pos = -1;
    LONG status = 0;
    LONG rc = -1;

    http[0] = 0;
    if (!mp_append(http, sizeof(http), &hp, "POST ") ||
        !mp_append(http, sizeof(http), &hp, cfg->path) ||
        !mp_append(http, sizeof(http), &hp, " HTTP/1.1\r\nHost: ") ||
        !mp_append(http, sizeof(http), &hp, cfg->host) ||
        !mp_append(http, sizeof(http), &hp, ":") ||
        !mp_append_ulong(http, sizeof(http), &hp, cfg->port) ||
        !mp_append(http, sizeof(http), &hp,
                   "\r\nContent-Type: application/ipp\r\nContent-Length: ") ||
        !mp_append_ulong(http, sizeof(http), &hp, ipp_size + file_size) ||
        !mp_append(http, sizeof(http), &hp,
                   "\r\nConnection: close\r\n\r\n")) {
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
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        { rc = -10; goto done; }

    if (!mp_safe_send(sock, (const UBYTE *)http, hp) ||
        !mp_safe_send(sock, ipp, ipp_size)) { rc = -11; goto done; }

    if (fh) {
        for (;;) {
            LONG got = Read(fh, filebuf, sizeof(filebuf));
            if (got < 0) { rc = -12; goto done; }
            if (got == 0) break;
            if (!mp_safe_send(sock, filebuf, (ULONG)got))
                { rc = -13; goto done; }
        }
    }

    while (response_used < sizeof(response)) {
        LONG got = recv(sock, (char *)(response + response_used),
                        (LONG)(sizeof(response) - response_used), 0);
        ULONG parsed_job_id = 0;
        if (got <= 0) break;
        response_used += (ULONG)got;
        body_pos = mp_final_http_body(response, response_used, &status);
        if (body_pos >= 0 &&
            response_used >= (ULONG)body_pos + 8UL &&
            (!need_job_id ||
             mp_ipp_find_job_id(response + body_pos,
                                response_used - (ULONG)body_pos,
                                &parsed_job_id))) {
            if (result && parsed_job_id) result->job_id = parsed_job_id;
            break;
        }
    }

    body_pos = mp_final_http_body(response, response_used, &status);
    if (body_pos < 0 || response_used < (ULONG)body_pos + 4UL)
        { rc = -14; goto done; }
    if (result) {
        result->http_status = status;
        result->ipp_status =
            (UWORD)(((UWORD)response[body_pos + 2] << 8) |
                    (UWORD)response[body_pos + 3]);
        if (need_job_id && result->job_id == 0)
            mp_ipp_find_job_id(response + body_pos,
                               response_used - (ULONG)body_pos,
                               &result->job_id);
    }
    if (status != 200) { rc = -15; goto done; }
    if (result && result->ipp_status >= 0x0100) { rc = -16; goto done; }
    if (need_job_id && (!result || result->job_id == 0))
        { rc = -17; goto done; }
    rc = 0;

done:
    if (sock >= 0) CloseSocket(sock);
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    if (result) result->error = rc;
    return rc;
}

LONG mp_ipp_print_document(const struct MPConfig *cfg, CONST_STRPTR filename,
                           CONST_STRPTR document_format,
                           struct MPIPPResult *result)
{
    BPTR fh = 0;
    LONG fsize;
    static UBYTE ipp[1024];
    ULONG io = 0;
    static char uri[192];
    LONG rc = -1;

    mp_ipp_result_init(result);
    if (!DOSBase || !cfg || !cfg->host[0] || cfg->port == 0 ||
        cfg->path[0] != '/' || !filename || !document_format) return -1;
    fh = Open(filename, MODE_OLDFILE);
    if (!fh) { rc = -2; goto done; }
    fsize = mp_file_size(fh);
    if (fsize <= 0) { rc = -3; goto done; }
    if (result) result->document_bytes = (ULONG)fsize;
    if (!mp_build_printer_uri(cfg, uri, sizeof(uri)))
        { rc = -4; goto done; }

    if (!mp_ipp_begin_request(ipp, sizeof(ipp), &io,
                              0x0002, uri, 0, TRUE) ||
        !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x49,
                     "document-format", (const char *)document_format) ||
        !mp_ipp_job_template(ipp, sizeof(ipp), &io, cfg, FALSE) ||
        !mp_put8(ipp, sizeof(ipp), &io, 0x03)) {
        rc = -5; goto done;
    }

    rc = mp_ipp_exchange(cfg, ipp, io, fh, (ULONG)fsize, FALSE, result);

done:
    if (fh) Close(fh);
    if (result) result->error = rc;
    return rc;
}

LONG mp_ipp_create_job(const struct MPConfig *cfg,
                       struct MPIPPResult *result)
{
    static UBYTE ipp[1024];
    ULONG io = 0;
    static char uri[192];
    LONG rc;

    mp_ipp_result_init(result);
    if (!DOSBase || !cfg || !cfg->host[0] || cfg->port == 0 ||
        cfg->path[0] != '/' || cfg->sides[0] != 't') return -1;
    if (!mp_build_printer_uri(cfg, uri, sizeof(uri))) return -4;

    if (!mp_ipp_begin_request(ipp, sizeof(ipp), &io,
                              0x0005, uri, 0, TRUE) ||
        !mp_ipp_job_template(ipp, sizeof(ipp), &io, cfg, TRUE) ||
        !mp_put8(ipp, sizeof(ipp), &io, 0x03)) return -5;

    rc = mp_ipp_exchange(cfg, ipp, io, 0, 0, TRUE, result);
    if (result) result->error = rc;
    return rc;
}

LONG mp_ipp_send_document(const struct MPConfig *cfg, ULONG job_id,
                          CONST_STRPTR filename,
                          CONST_STRPTR document_format,
                          BOOL last_document,
                          struct MPIPPResult *result)
{
    BPTR fh = 0;
    LONG fsize = 0;
    static UBYTE ipp[1024];
    ULONG io = 0;
    static char uri[192];
    LONG rc = -1;

    mp_ipp_result_init(result);
    if (!DOSBase || !cfg || !cfg->host[0] || cfg->port == 0 ||
        cfg->path[0] != '/' || job_id == 0) return -1;
    if (filename) {
        if (!document_format) return -1;
        fh = Open(filename, MODE_OLDFILE);
        if (!fh) { rc = -2; goto done; }
        fsize = mp_file_size(fh);
        if (fsize <= 0) { rc = -3; goto done; }
        if (result) result->document_bytes = (ULONG)fsize;
    } else if (!last_document) {
        return -1;
    }
    if (!mp_build_printer_uri(cfg, uri, sizeof(uri)))
        { rc = -4; goto done; }

    if (!mp_ipp_begin_request(ipp, sizeof(ipp), &io,
                              0x0006, uri, job_id, FALSE) ||
        (fh && !mp_ipp_attr(ipp, sizeof(ipp), &io, 0x49,
                            "document-format",
                            (const char *)document_format)) ||
        !mp_ipp_boolean_attr(ipp, sizeof(ipp), &io,
                             "last-document", last_document) ||
        !mp_put8(ipp, sizeof(ipp), &io, 0x03)) {
        rc = -5; goto done;
    }

    rc = mp_ipp_exchange(cfg, ipp, io, fh, (ULONG)fsize, FALSE, result);

done:
    if (fh) Close(fh);
    if (result) result->error = rc;
    return rc;
}
