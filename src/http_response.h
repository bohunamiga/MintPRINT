#ifndef MINTPRINT_HTTP_RESPONSE_H
#define MINTPRINT_HTTP_RESPONSE_H

/* Small, allocation-free HTTP/1.x response helpers used by MintPrint
 * Settings. Lengths and offsets are byte counts; functions return -1 for
 * malformed input where documented. */
int mp_http_find_body(const char *buf, int len, int start);
int mp_http_status(const char *buf, int len, int start);
int mp_http_content_length(const char *buf, int header_start, int body_off);
int mp_http_header_has_token(const char *buf, int header_start, int body_off,
                             const char *header_name, const char *token);
int mp_http_chunked_complete(const char *body, int encoded_len);
int mp_http_decode_chunked(char *body, int encoded_len);

/* Locate the final (non-1xx) HTTP response and expose its decoded body.
 * Chunked bodies are decoded in place. Returns 1 when the response is
 * complete, 0 when more bytes are required, and -1 for malformed framing. */
int mp_http_final_body(char *buf, int len, int *http_status,
                       int *body_off, int *body_len);

#endif
