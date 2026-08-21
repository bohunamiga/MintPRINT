#include "http_response.h"

#include <string.h>

static char mp_http_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int mp_http_equal_n(const char *a, const char *b, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        if (mp_http_lower(a[i]) != mp_http_lower(b[i])) return 0;
    }
    return 1;
}

static int mp_http_line_end(const char *buf, int start, int limit)
{
    int i;
    for (i = start; i + 1 < limit; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return i;
    }
    return -1;
}

int mp_http_find_body(const char *buf, int len, int start)
{
    int i;
    if (!buf || len < 4 || start < 0 || start + 4 > len) return -1;
    for (i = start; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    return -1;
}

int mp_http_status(const char *buf, int len, int start)
{
    int i = start;
    if (!buf || start < 0 || start >= len) return 0;
    while (i < len && buf[i] != ' ') ++i;
    if (i + 4 > len) return 0;
    if (buf[i + 1] < '0' || buf[i + 1] > '9' ||
        buf[i + 2] < '0' || buf[i + 2] > '9' ||
        buf[i + 3] < '0' || buf[i + 3] > '9') return 0;
    return (buf[i + 1] - '0') * 100 +
           (buf[i + 2] - '0') * 10 + (buf[i + 3] - '0');
}

static int mp_http_header_value(const char *buf, int header_start,
                                int body_off, const char *header_name,
                                int *value_start, int *value_len)
{
    int name_len;
    int line;
    int line_end;
    int colon;
    int end;

    if (!buf || !header_name || header_start < 0 || body_off < header_start + 4)
        return 0;

    name_len = (int)strlen(header_name);
    line_end = mp_http_line_end(buf, header_start, body_off);
    if (line_end < 0) return 0;
    line = line_end + 2; /* Skip the status line. */
    end = body_off - 2;  /* Start of the empty line ending the header. */

    while (line < end) {
        int first;
        int last;

        line_end = mp_http_line_end(buf, line, body_off);
        if (line_end < 0 || line_end == line) break;

        colon = line;
        while (colon < line_end && buf[colon] != ':') ++colon;
        if (colon - line == name_len && colon < line_end &&
            mp_http_equal_n(buf + line, header_name, name_len)) {
            first = colon + 1;
            while (first < line_end && (buf[first] == ' ' || buf[first] == '\t'))
                ++first;
            last = line_end;
            while (last > first && (buf[last - 1] == ' ' || buf[last - 1] == '\t'))
                --last;
            if (value_start) *value_start = first;
            if (value_len) *value_len = last - first;
            return 1;
        }
        line = line_end + 2;
    }
    return 0;
}

int mp_http_content_length(const char *buf, int header_start, int body_off)
{
    int start;
    int len;
    int i;
    int value = 0;

    if (!mp_http_header_value(buf, header_start, body_off, "Content-Length",
                              &start, &len) || len <= 0)
        return -1;

    for (i = 0; i < len; ++i) {
        int digit;
        if (buf[start + i] < '0' || buf[start + i] > '9') return -1;
        digit = buf[start + i] - '0';
        if (value > 25000000) return -1;
        value = value * 10 + digit;
    }
    return value;
}

int mp_http_header_has_token(const char *buf, int header_start, int body_off,
                             const char *header_name, const char *token)
{
    int start;
    int len;
    int token_len;
    int pos = 0;

    if (!token || !mp_http_header_value(buf, header_start, body_off,
                                        header_name, &start, &len))
        return 0;
    token_len = (int)strlen(token);

    while (pos < len) {
        int first;
        int last;
        while (pos < len && (buf[start + pos] == ' ' ||
                             buf[start + pos] == '\t' ||
                             buf[start + pos] == ',')) ++pos;
        first = pos;
        while (pos < len && buf[start + pos] != ',' &&
               buf[start + pos] != ';') ++pos;
        last = pos;
        while (last > first && (buf[start + last - 1] == ' ' ||
                                buf[start + last - 1] == '\t')) --last;
        if (last - first == token_len &&
            mp_http_equal_n(buf + start + first, token, token_len))
            return 1;
        while (pos < len && buf[start + pos] != ',') ++pos;
    }
    return 0;
}

static int mp_http_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = mp_http_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Returns 1 when the terminal zero chunk and trailer terminator are present,
 * 0 when more bytes are needed, and -1 for malformed chunk framing. */
int mp_http_chunked_complete(const char *body, int encoded_len)
{
    int src = 0;

    if (!body || encoded_len < 0) return -1;
    for (;;) {
        int line_end = mp_http_line_end(body, src, encoded_len);
        int pos;
        int digits = 0;
        unsigned long size = 0;

        if (line_end < 0) return 0;
        pos = src;
        while (pos < line_end && body[pos] != ';' &&
               body[pos] != ' ' && body[pos] != '\t') {
            int hex = mp_http_hex(body[pos]);
            if (hex < 0 || size > 0x0fffffffUL) return -1;
            size = size * 16UL + (unsigned long)hex;
            ++digits;
            ++pos;
        }
        if (!digits) return -1;
        src = line_end + 2;

        if (size == 0) {
            int i;
            if (src + 2 <= encoded_len && body[src] == '\r' && body[src + 1] == '\n')
                return 1;
            for (i = src; i + 3 < encoded_len; ++i) {
                if (body[i] == '\r' && body[i + 1] == '\n' &&
                    body[i + 2] == '\r' && body[i + 3] == '\n')
                    return 1;
            }
            return 0;
        }

        if (size > (unsigned long)(encoded_len - src)) return 0;
        src += (int)size;
        if (src + 2 > encoded_len) return 0;
        if (body[src] != '\r' || body[src + 1] != '\n') return -1;
        src += 2;
    }
}

/* Decodes a complete chunked body in place and returns its payload length. */
int mp_http_decode_chunked(char *body, int encoded_len)
{
    int src = 0;
    int dst = 0;

    if (mp_http_chunked_complete(body, encoded_len) != 1) return -1;
    for (;;) {
        int line_end = mp_http_line_end(body, src, encoded_len);
        int pos = src;
        unsigned long size = 0;

        while (pos < line_end && body[pos] != ';' &&
               body[pos] != ' ' && body[pos] != '\t') {
            size = size * 16UL + (unsigned long)mp_http_hex(body[pos]);
            ++pos;
        }
        src = line_end + 2;
        if (size == 0) return dst;
        memmove(body + dst, body + src, (size_t)size);
        dst += (int)size;
        src += (int)size + 2;
    }
}
