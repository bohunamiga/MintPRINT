#include "ipp_enum.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    static const unsigned char draft[4]  = { 0x00, 0x00, 0x00, 0x03 };
    static const unsigned char normal[4] = { 0x00, 0x00, 0x00, 0x04 };
    static const unsigned char high[4]   = { 0x00, 0x00, 0x00, 0x05 };
    static const unsigned char mixed[4]  = { 0x12, 0x34, 0x56, 0x78 };
    static const unsigned char max[4]    = { 0xFF, 0xFF, 0xFF, 0xFF };

    /* print-quality-supported (RFC 8011 5.4.13) enum values. */
    assert(mp_ipp_decode_be32(draft) == 3UL);
    assert(mp_ipp_decode_be32(normal) == 4UL);
    assert(mp_ipp_decode_be32(high) == 5UL);

    /* General big-endian decoding, independent of any known enum. */
    assert(mp_ipp_decode_be32(mixed) == 0x12345678UL);
    assert(mp_ipp_decode_be32(max) == 0xFFFFFFFFUL);

    puts("IPP enum decode tests passed");
    return 0;
}
