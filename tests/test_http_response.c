#include "http_response.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_interim_and_case_insensitive_length(void)
{
    static const char response[] =
        "HTTP/1.1 100 Continue\r\n\r\n"
        "HTTP/1.1 200 OK\r\n"
        "content-length: 8\r\n"
        "Content-Type: application/ipp\r\n\r\n"
        "12345678";
    int first_body = mp_http_find_body(response, (int)sizeof(response) - 1, 0);
    int final_body;

    assert(first_body > 0);
    assert(mp_http_status(response, (int)sizeof(response) - 1, 0) == 100);
    final_body = mp_http_find_body(response, (int)sizeof(response) - 1, first_body);
    assert(final_body > first_body);
    assert(mp_http_status(response, (int)sizeof(response) - 1, first_body) == 200);
    assert(mp_http_content_length(response, first_body, final_body) == 8);
}

static void test_chunked_body(void)
{
    static const char expected[] = { 1, 1, 0, 0, 0, 0, 0, 1 };
    char body[] =
        "4\r\n\x01\x01\x00\x00\r\n"
        "4;printer=yes\r\n\x00\x00\x00\x01\r\n"
        "0\r\nX-Test: yes\r\n\r\n";
    int encoded_len = (int)sizeof(body) - 1;
    int decoded_len;
    int prefix_len;

    for (prefix_len = 0; prefix_len < encoded_len; ++prefix_len)
        assert(mp_http_chunked_complete(body, prefix_len) == 0);
    assert(mp_http_chunked_complete(body, encoded_len) == 1);
    decoded_len = mp_http_decode_chunked(body, encoded_len);
    assert(decoded_len == (int)sizeof(expected));
    assert(memcmp(body, expected, sizeof(expected)) == 0);
}

static void test_transfer_encoding_token(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: gzip, Chunked\r\n\r\n";
    int body = mp_http_find_body(response, (int)sizeof(response) - 1, 0);

    assert(body > 0);
    assert(mp_http_header_has_token(response, 0, body,
                                    "Transfer-Encoding", "chunked") == 1);
}

int main(void)
{
    test_interim_and_case_insensitive_length();
    test_chunked_body();
    test_transfer_encoding_token();
    puts("HTTP response parser tests passed");
    return 0;
}
