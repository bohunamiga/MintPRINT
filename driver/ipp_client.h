#ifndef MINTPRINT_IPP_CLIENT_H
#define MINTPRINT_IPP_CLIENT_H

#include <exec/types.h>
#include "config.h"

struct MPIPPResult {
    LONG error;
    LONG http_status;
    UWORD ipp_status;
    ULONG document_bytes;
};

LONG mp_ipp_print_document(const struct MPConfig *cfg, CONST_STRPTR filename,
                           CONST_STRPTR document_format,
                           struct MPIPPResult *result);

/* Parses the self-describing dimensions at the end of a PWG media name,
 * e.g. iso_a4_210x297mm or na_letter_8.5x11in, into x/y in hundredths of
 * a millimetre (IPP media-size units). Returns 0 (and leaves *x/*y
 * untouched) if media isn't in that form. */
int mp_media_dimensions(const char *media, ULONG *x, ULONG *y);

#endif
