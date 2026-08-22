#ifndef MINTPRINT_IPP_ENUM_H
#define MINTPRINT_IPP_ENUM_H

/* Decodes a 4-byte big-endian IPP integer/enum value (RFC 8010 3.5.2) from
 * raw wire bytes, e.g. the value of print-quality-supported (value tag
 * 0x23, 'enum') or printer-state (also 'enum'). Callers must ensure raw
 * points at exactly 4 readable bytes - the attribute's own value_len. */
unsigned long mp_ipp_decode_be32(const unsigned char *raw);

#endif
