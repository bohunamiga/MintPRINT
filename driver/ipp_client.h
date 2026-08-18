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

LONG mp_ipp_print_jpeg(const struct MPConfig *cfg, CONST_STRPTR filename,
                       struct MPIPPResult *result);

#endif
