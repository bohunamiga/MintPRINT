#ifndef MINTPRINT_IPP_CLIENT_H
#define MINTPRINT_IPP_CLIENT_H

#include <exec/types.h>
#include "config.h"

struct MPIPPResult {
    LONG error;
    LONG http_status;
    UWORD ipp_status;
    ULONG document_bytes;
    ULONG job_id;
};

/* Opens bsdsocket.library V4 and creates a harmless unconnected socket.
 * The dedicated spool Process calls this during driver startup so socket
 * functions never run from an arbitrary printer.device caller Task. */
BOOL mp_ipp_socket_available(void);

LONG mp_ipp_print_document(const struct MPConfig *cfg, CONST_STRPTR filename,
                           CONST_STRPTR document_format,
                           struct MPIPPResult *result);

/* Duplex uses one IPP Job containing one Send-Document per Amiga page.
 * Create-Job carries sides= plus multiple-document-handling=single-document;
 * the final empty Send-Document with last-document=true closes the Job. */
LONG mp_ipp_create_job(const struct MPConfig *cfg,
                       struct MPIPPResult *result);
LONG mp_ipp_send_document(const struct MPConfig *cfg, ULONG job_id,
                          CONST_STRPTR filename,
                          CONST_STRPTR document_format,
                          BOOL last_document,
                          struct MPIPPResult *result);

#endif
