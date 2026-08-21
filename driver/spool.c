/*
 * See spool.h for why this exists. Everything in this file that touches
 * dos.library (Open/Write/Seek/Close/DeleteFile, mp_ipp_print_document
 * which itself reopens the finished job file, and mp_config_load which
 * reads ENV:/ENVARC:) runs only inside
 * mp_spool_entry() - the body of a dedicated Process this file spawns via
 * CreateNewProc(). Every other function here runs in whatever context the
 * caller (driver_core.c's Init/DriverOpen/DriverClose/Render, therefore
 * whatever task the calling application used) gives it, and touches only
 * exec.library primitives, which are safe from any Task.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "spool.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

#define MP_SPOOL_PORT_NAME ((CONST_STRPTR)"MintPRINT.spool")

enum {
    MP_SPOOL_CMD_LOG = 1,
    MP_SPOOL_CMD_JOB_OPEN,
    MP_SPOOL_CMD_JOB_WRITE,
    MP_SPOOL_CMD_JOB_PATCH,
    MP_SPOOL_CMD_JOB_CLOSE,
    MP_SPOOL_CMD_JOB_DELETE,
    MP_SPOOL_CMD_IPP_SUBMIT,
    MP_SPOOL_CMD_IPP_CREATE_JOB,
    MP_SPOOL_CMD_IPP_SEND_DOCUMENT,
    MP_SPOOL_CMD_CONFIG_LOAD,
    MP_SPOOL_CMD_QUIT
};

struct MPSpoolMsg {
    struct Message mn;
    UWORD cmd;
    CONST_STRPTR filename;
    const UBYTE *data;
    ULONG length;
    ULONG offset;
    const char *text;
    const struct MPConfig *cfg;
    struct MPConfig *cfg_io;
    CONST_STRPTR document_format;
    ULONG job_id;
    BOOL last_document;
    struct MPIPPResult ipp_result;
    LONG result;
};

static struct MsgPort *g_spool_port = NULL;
static struct MsgPort *g_spool_startup_port = NULL;
static struct Message g_spool_ready_msg;

/* ------------------------------------------------------------------- *
 * Spool process body. Everything below this point up to mp_spool_entry
 * itself runs ONLY inside the spool process.
 * ------------------------------------------------------------------- */

static void mp_spool_proc_append_log(const char *text)
{
    BPTR fh;
    ULONG len;

    if (!DOSBase || !text) return;

    fh = Open((CONST_STRPTR)"T:MintPRINT-driver.log", MODE_READWRITE);
    if (!fh) fh = Open((CONST_STRPTR)"T:MintPRINT-driver.log", MODE_NEWFILE);
    if (!fh) return;

    Seek(fh, 0, OFFSET_END);
    for (len = 0; text[len]; ++len) { /* count */ }
    if (len) Write(fh, (APTR)text, (LONG)len);
    Write(fh, (APTR)"\n", 1);
    Close(fh);
}

static void mp_spool_proc_close_job(BPTR *job_fh)
{
    if (*job_fh) {
        Close(*job_fh);
        *job_fh = 0;
    }
}

static LONG mp_spool_entry(void)
{
    struct MsgPort *port;
    struct MsgPort *startup_reply_to = g_spool_startup_port;
    struct MPSpoolMsg *quit_msg = NULL;
    BOOL running = TRUE;
    BPTR job_fh = 0;

    /* This is a real AmigaDOS Process, unlike an arbitrary caller of a
     * printer.device callback. Probe bsdsocket here so socket() is never
     * invoked from a potentially bare Task. If the probe fails, leave port
     * NULL; the normal startup handshake below wakes Init(), which then
     * rejects the driver cleanly before raster generation starts. */
    port = NULL;
    if (mp_ipp_socket_available())
        port = CreateMsgPort();
    if (port) {
        port->mp_Node.ln_Name = (char *)MP_SPOOL_PORT_NAME;
        port->mp_Node.ln_Pri = 0;
        Forbid();
        AddPort(port);
        Permit();
    }

    /* Tell the parent we're ready (or that we failed) either way - it is
     * blocked in WaitPort() waiting for exactly this. */
    if (startup_reply_to) {
        g_spool_ready_msg.mn_Node.ln_Type = NT_MESSAGE;
        g_spool_ready_msg.mn_Length = sizeof(g_spool_ready_msg);
        g_spool_ready_msg.mn_ReplyPort = NULL;
        PutMsg(startup_reply_to, &g_spool_ready_msg);
    }

    if (!port) return 0;

    while (running) {
        struct MPSpoolMsg *m;

        WaitPort(port);
        while ((m = (struct MPSpoolMsg *)GetMsg(port)) != NULL) {
            switch (m->cmd) {
                case MP_SPOOL_CMD_LOG:
                    mp_spool_proc_append_log(m->text);
                    m->result = 0;
                    break;

                case MP_SPOOL_CMD_JOB_OPEN:
                    mp_spool_proc_close_job(&job_fh);
                    job_fh = Open(m->filename, MODE_NEWFILE);
                    m->result = job_fh ? 0 : -1;
                    break;

                case MP_SPOOL_CMD_JOB_WRITE:
                    if (job_fh && m->data && m->length) {
                        ULONG done = 0;
                        m->result = 0;
                        while (done < m->length) {
                            LONG n = Write(job_fh, (APTR)(m->data + done),
                                          (LONG)(m->length - done));
                            if (n <= 0) { m->result = -1; break; }
                            done += (ULONG)n;
                        }
                    } else {
                        m->result = -1;
                    }
                    break;

                case MP_SPOOL_CMD_JOB_PATCH:
                    if (job_fh && m->data && m->length &&
                        Seek(job_fh, (LONG)m->offset, OFFSET_BEGINNING) != -1) {
                        LONG n = Write(job_fh, (APTR)m->data, (LONG)m->length);
                        m->result = (n == (LONG)m->length) ? 0 : -1;
                        Seek(job_fh, 0, OFFSET_END);
                    } else {
                        m->result = -1;
                    }
                    break;

                case MP_SPOOL_CMD_JOB_CLOSE:
                    mp_spool_proc_close_job(&job_fh);
                    m->result = 0;
                    break;

                case MP_SPOOL_CMD_JOB_DELETE:
                    m->result = DeleteFile(m->filename) ? 0 : -1;
                    break;

                case MP_SPOOL_CMD_IPP_SUBMIT:
                    m->result = mp_ipp_print_document(m->cfg, m->filename,
                                                      m->document_format,
                                                      &m->ipp_result);
                    break;

                case MP_SPOOL_CMD_IPP_CREATE_JOB:
                    m->result = mp_ipp_create_job(m->cfg, &m->ipp_result);
                    break;

                case MP_SPOOL_CMD_IPP_SEND_DOCUMENT:
                    m->result = mp_ipp_send_document(m->cfg, m->job_id,
                                                      m->filename,
                                                      m->document_format,
                                                      m->last_document,
                                                      &m->ipp_result);
                    break;

                case MP_SPOOL_CMD_CONFIG_LOAD:
                    m->result = mp_config_load(m->cfg_io);
                    break;

                case MP_SPOOL_CMD_QUIT:
                default:
                    /*
                     * Do not wake the shutdown caller yet.
                     *
                     * This Process is executing code from the MintPRINT
                     * driver segment.  If we ReplyMsg() here, classic
                     * printer.device can continue through CloseDevice(),
                     * call Expunge(), and unload that segment while this
                     * Process is still executing its teardown.
                     *
                     * Keep the caller blocked until the public spool port
                     * has been removed/deleted.  The final reply is sent as
                     * the last useful action before this entry function
                     * returns to dos.library's process startup code.
                     */
                    mp_spool_proc_close_job(&job_fh);
                    m->result = 0;
                    quit_msg = m;
                    running = FALSE;
                    break;
            }

            if (m != quit_msg)
                ReplyMsg((struct Message *)m);

            if (!running)
                break;
        }
    }

    Forbid();
    RemPort(port);
    Permit();
    DeleteMsgPort(port);

    /*
     * The worker is created above the normal caller's priority.  Once this
     * reply wakes CloseDevice()/Expunge, the worker therefore keeps the CPU
     * long enough to complete the tiny return epilogue and leave the driver
     * segment before the caller can unload it.
     */
    if (quit_msg)
        ReplyMsg((struct Message *)quit_msg);

    return 0;
}

/* ------------------------------------------------------------------- *
 * Client side. Runs in the caller's context - exec.library only, never
 * dos.library (CreateNewProc is the one confirmed-safe exception: it is
 * exec/dos.library's own supported way to bootstrap a process from a
 * plain Task).
 * ------------------------------------------------------------------- */

static struct MsgPort *mp_spool_find_port(void)
{
    struct MsgPort *p;
    Forbid();
    p = FindPort(MP_SPOOL_PORT_NAME);
    Permit();
    return p;
}

BOOL mp_spool_ensure_running(void)
{
    struct MsgPort *startup_port;

    g_spool_port = mp_spool_find_port();
    if (g_spool_port) return TRUE;

    startup_port = CreateMsgPort();
    if (!startup_port) return FALSE;

    g_spool_startup_port = startup_port;

    /* CreateNewProc()'s undocumented-to-first-glance danger: any of these
     * tags left unspecified default to "duplicate the CALLING process's own
     * field" (current dir, home dir, console task, window pointer) - which
     * means dereferencing pr_CurrentDir/pr_HomeDir/pr_ConsoleTask/
     * pr_WindowPtr on whatever FindTask(NULL) returns, cast straight to
     * struct Process* with no check that it actually is one. From a bare
     * Task (exactly the DPaint background-print case this file exists for)
     * those fields do not exist, so the "duplicate from caller" default is
     * itself an unsafe dos.library access - pin every one of them
     * explicitly so CreateNewProc never reads anything from the caller. */
    if (!CreateNewProcTags(NP_Entry, (ULONG)mp_spool_entry,
                       NP_StackSize, 8192L,
                       NP_Name, (ULONG)"MintPRINT spool",
                       /* See deferred QUIT reply in mp_spool_entry(). */
                       NP_Priority, 1L,
                       NP_CurrentDir, 0L,
                       NP_Input, 0L,
                       NP_CloseInput, FALSE,
                       NP_Output, 0L,
                       NP_CloseOutput, FALSE,
                       NP_ConsoleTask, 0L,
                       NP_WindowPtr, -1L,
                       TAG_DONE)) {
        g_spool_startup_port = NULL;
        DeleteMsgPort(startup_port);
        return FALSE;
    }

    WaitPort(startup_port);
    GetMsg(startup_port);

    g_spool_startup_port = NULL;
    DeleteMsgPort(startup_port);

    g_spool_port = mp_spool_find_port();
    return g_spool_port != NULL;
}

static BOOL mp_spool_send(struct MPSpoolMsg *m)
{
    struct MsgPort *reply_port;

    if (!g_spool_port && !mp_spool_ensure_running()) return FALSE;
    if (!g_spool_port) return FALSE;

    reply_port = CreateMsgPort();
    if (!reply_port) return FALSE;

    m->mn.mn_Node.ln_Type = NT_MESSAGE;
    m->mn.mn_Length = sizeof(*m);
    m->mn.mn_ReplyPort = reply_port;

    PutMsg(g_spool_port, (struct Message *)m);
    WaitPort(reply_port);
    GetMsg(reply_port);

    DeleteMsgPort(reply_port);

    return m->result == 0;
}

void mp_spool_log(const char *line)
{
    struct MPSpoolMsg m;
    if (!line) return;
    m.cmd = MP_SPOOL_CMD_LOG;
    m.text = line;
    mp_spool_send(&m); /* best-effort: a lost log line never fails the job */
}

BOOL mp_spool_job_open(CONST_STRPTR filename)
{
    struct MPSpoolMsg m;
    m.cmd = MP_SPOOL_CMD_JOB_OPEN;
    m.filename = filename;
    return mp_spool_send(&m);
}

BOOL mp_spool_job_write(const UBYTE *data, ULONG length)
{
    struct MPSpoolMsg m;
    m.cmd = MP_SPOOL_CMD_JOB_WRITE;
    m.data = data;
    m.length = length;
    return mp_spool_send(&m);
}

BOOL mp_spool_job_patch(ULONG offset, const UBYTE *data, ULONG length)
{
    struct MPSpoolMsg m;
    m.cmd = MP_SPOOL_CMD_JOB_PATCH;
    m.offset = offset;
    m.data = data;
    m.length = length;
    return mp_spool_send(&m);
}

void mp_spool_job_close(void)
{
    struct MPSpoolMsg m;
    m.cmd = MP_SPOOL_CMD_JOB_CLOSE;
    mp_spool_send(&m);
}

void mp_spool_job_delete(CONST_STRPTR filename)
{
    struct MPSpoolMsg m;
    m.cmd = MP_SPOOL_CMD_JOB_DELETE;
    m.filename = filename;
    mp_spool_send(&m);
}

LONG mp_spool_ipp_submit(const struct MPConfig *cfg, CONST_STRPTR filename,
                         CONST_STRPTR document_format,
                         struct MPIPPResult *result)
{
    struct MPSpoolMsg m;

    m.cmd = MP_SPOOL_CMD_IPP_SUBMIT;
    m.cfg = cfg;
    m.filename = filename;
    m.document_format = document_format;
    m.ipp_result.error = -1;
    m.ipp_result.http_status = 0;
    m.ipp_result.ipp_status = 0xffff;
    m.ipp_result.document_bytes = 0;
    m.ipp_result.job_id = 0;
    m.result = -1;

    mp_spool_send(&m); /* m.result carries mp_ipp_print_document()'s return */

    if (result) *result = m.ipp_result;
    return m.result;
}

LONG mp_spool_ipp_create_job(const struct MPConfig *cfg,
                             struct MPIPPResult *result)
{
    struct MPSpoolMsg m;

    m.cmd = MP_SPOOL_CMD_IPP_CREATE_JOB;
    m.cfg = cfg;
    m.ipp_result.error = -1;
    m.ipp_result.http_status = 0;
    m.ipp_result.ipp_status = 0xffff;
    m.ipp_result.document_bytes = 0;
    m.ipp_result.job_id = 0;
    m.result = -1;

    mp_spool_send(&m);

    if (result) *result = m.ipp_result;
    return m.result;
}

LONG mp_spool_ipp_send_document(const struct MPConfig *cfg, ULONG job_id,
                                CONST_STRPTR filename,
                                CONST_STRPTR document_format,
                                BOOL last_document,
                                struct MPIPPResult *result)
{
    struct MPSpoolMsg m;

    m.cmd = MP_SPOOL_CMD_IPP_SEND_DOCUMENT;
    m.cfg = cfg;
    m.job_id = job_id;
    m.filename = filename;
    m.document_format = document_format;
    m.last_document = last_document;
    m.ipp_result.error = -1;
    m.ipp_result.http_status = 0;
    m.ipp_result.ipp_status = 0xffff;
    m.ipp_result.document_bytes = 0;
    m.ipp_result.job_id = job_id;
    m.result = -1;

    mp_spool_send(&m);

    if (result) *result = m.ipp_result;
    return m.result;
}

LONG mp_spool_config_load(struct MPConfig *cfg)
{
    struct MPSpoolMsg m;

    if (!cfg) return MP_CONFIG_SOURCE_DEFAULTS;

    /* Task-safe fallback: pure memory, no dos.library. Overwritten below if
     * the spool process is reachable and does the real ENV:/ENVARC: load. */
    mp_config_defaults(cfg);

    m.cmd = MP_SPOOL_CMD_CONFIG_LOAD;
    m.cfg_io = cfg;
    /* mp_spool_send()'s BOOL return means "result==0", but 0 is itself a
     * valid MP_CONFIG_SOURCE_* (defaults) here, not a failure code - so use
     * a sentinel to tell "never delivered" apart from "delivered, and the
     * spool process reported defaults were used". */
    m.result = -1;
    mp_spool_send(&m);

    return (m.result >= 0) ? m.result : MP_CONFIG_SOURCE_DEFAULTS;
}

void mp_spool_shutdown(void)
{
    struct MPSpoolMsg m;
    if (!g_spool_port) g_spool_port = mp_spool_find_port();
    if (!g_spool_port) return;
    m.cmd = MP_SPOOL_CMD_QUIT;
    mp_spool_send(&m);
    g_spool_port = NULL;
}
