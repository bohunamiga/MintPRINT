#ifndef MINTPRINT_IPP_CLIENT_H
#define MINTPRINT_IPP_CLIENT_H

#include <exec/types.h>

struct MPIPPResult {
    LONG error;
    LONG http_status;
    UWORD ipp_status;
    ULONG document_bytes;
};

/*
 * Spike #3 defaults.  The Prefs application will replace these with an
 * ENV:/ENVARC: profile in a later patch.
 */
#define MP_IPP_HOST "192.168.0.51"
#define MP_IPP_PORT 80
#define MP_IPP_PATH "/ipp/print"

LONG mp_ipp_print_jpeg(CONST_STRPTR filename, struct MPIPPResult *result);

#endif
