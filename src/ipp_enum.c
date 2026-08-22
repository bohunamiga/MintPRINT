#include "ipp_enum.h"

unsigned long mp_ipp_decode_be32(const unsigned char *raw)
{
    return ((unsigned long)raw[0] << 24) |
           ((unsigned long)raw[1] << 16) |
           ((unsigned long)raw[2] << 8) |
           (unsigned long)raw[3];
}
