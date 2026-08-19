#ifndef MINTPRINT_SPOOL_H
#define MINTPRINT_SPOOL_H

/*
 * dos.library functions may only be called from a Process, not a bare
 * Exec Task - and printer.device explicitly documents that a driver's
 * callbacks (Init/DriverOpen/DriverClose/Render) run "in the context of
 * the requesting task", i.e. whatever task/process the calling
 * application used to open printer.device. Well-behaved AmigaDOS
 * programs are Processes themselves, so this works fine for most
 * callers - but at least one real caller (DPaint, printing via a
 * background Task rather than its own Process) hits this directly:
 * every dos.library call this driver made from inside those callbacks
 * (Open/Write/Seek/Close for the log and job spool files) was a
 * correctness bug waiting for the wrong kind of caller, surfacing as an
 * unpredictable CPU exception (illegal instruction, address error, ...)
 * from memory corruption rather than a clean, diagnosable error.
 *
 * The fix: one small dedicated Process, spawned lazily via
 * CreateNewProc() (itself an exec-callable bootstrap, safe from a Task),
 * owns every dos.library call the driver needs - the log file, the job
 * spool file, and IPP submission (which also reads the finished job file
 * back off disk). The driver's callbacks, still running in whatever
 * context the caller gave them, only ever touch exec.library primitives
 * (CreateMsgPort/PutMsg/WaitPort/GetMsg/DeleteMsgPort, all Task-safe) to
 * hand a request to that process and block for its reply - never dos.library
 * directly.
 */

#include <exec/types.h>
#include "config.h"
#include "ipp_client.h"

BOOL mp_spool_ensure_running(void);
void mp_spool_shutdown(void);

/* Best-effort: logging never blocks the job on failure. */
void mp_spool_log(const char *line);

/* Runs mp_config_load() (ENV:/ENVARC: Open/FGets/Close) inside the spool
 * process; on failure to reach the spool process cfg is still left holding
 * plain in-memory defaults (mp_config_defaults() touches no dos.library
 * call, so that fallback is always Task-safe). Returns the MP_CONFIG_SOURCE_*
 * that was actually loaded. */
LONG mp_spool_config_load(struct MPConfig *cfg);

BOOL mp_spool_job_open(CONST_STRPTR filename);
BOOL mp_spool_job_write(const UBYTE *data, ULONG length);
void mp_spool_job_close(void);
void mp_spool_job_delete(CONST_STRPTR filename);

/* Runs mp_ipp_print_document() inside the spool process, since it also
 * needs dos.library to reopen and read back the finished job file. */
LONG mp_spool_ipp_submit(const struct MPConfig *cfg, CONST_STRPTR filename,
                         CONST_STRPTR document_format,
                         struct MPIPPResult *result);

#endif
