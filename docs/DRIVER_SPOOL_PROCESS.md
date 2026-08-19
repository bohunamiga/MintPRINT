# MintPRINT driver: background spool process (Task/Process DOS-safety fix)

## The bug

`DEVS:Printers/MintPRINT` crashed reliably when printing from DPaint (error
`#8000000A`, a CPU exception), while MultiView and GraphicDump printed the
same jobs through the same driver code without issue.

AmigaOS's `printer.device` documents that a driver's callbacks
(`Init`/`DriverOpen`/`DriverClose`/`Render`/etc.) run "in the context of the
requesting task" - i.e. whatever Task or Process the calling application used
to open `printer.device`. Most well-behaved AmigaDOS applications are
Processes, so this is invisible. `dos.library` functions, however, are
documented as only safe to call from a Process, not a bare Exec Task -
calling them from a Task can corrupt memory or crash with exactly the kind
of unpredictable CPU exception seen here, rather than a clean error.

DPaint prints via a background Task rather than its own Process (a known
pattern for that era's background-printing features). Every previous version
of this driver called `dos.library` directly from inside its callbacks -
`Open`/`Write`/`Seek`/`Close`/`DeleteFile` for the trace log and the spooled
job file, and (via `mp_config_load`) again for reading `ENV:MintPRINT/Unit0`.
All of that ran fine from MultiView/GraphicDump's Process context and was a
correctness bug waiting for the wrong kind of caller under DPaint's Task
context.

## The fix

`driver/spool.c` / `driver/spool.h` add one small dedicated background
Process, spawned lazily via `CreateNewProc()` - itself an exec-callable
bootstrap that is specifically documented as safe to call from a bare Task,
used here for exactly that purpose. This process owns every remaining
`dos.library` call in the driver:

- the trace log (`T:MintPRINT-driver.log`)
- the spooled job file (`T:MintPRINT-job.jpg` / `T:MintPRINT-job.pwg`) -
  open, write, close, delete
- `mp_config_load()` (reads `ENV:MintPRINT/Unit0` or `ENVARC:MintPRINT/Unit0`)
- `mp_ipp_print_document()` (which itself reopens the finished job file to
  read it back for the IPP `Print-Job` request)

`driver/driver_core.c`'s callbacks - still running in whatever context the
calling application gives them - no longer call `dos.library` at all. They
only use `exec.library` primitives (`CreateMsgPort`/`PutMsg`/`WaitPort`/
`GetMsg`/`DeleteMsgPort`/`Forbid`/`Permit`), all documented as Task-safe, to
hand a request to the spool process via a named `MsgPort`
(`"MintPRINT.spool"`) and block for its reply. The spool process is started
once from `Init()` (`mp_spool_ensure_running()`) and stopped from `Expunge()`
(`mp_spool_shutdown()`); `mp_spool_send()` also starts it lazily on first use
if that hasn't happened yet.

One exception, and it is a safe one: `mp_config_defaults()` (plain
in-memory struct field assignment, no `dos.library` call) still runs
directly in the caller's context as a fallback so a config struct is never
left uninitialised if the spool process cannot be reached.

## Status: implemented, not yet physically test-printed

This fix was derived and implemented from an independent check of the
AmigaOS Task/Process/`dos.library` rules, cross-referenced against
documented `printer.device` driver callback behaviour, and against the
empirical evidence that the crash was caller-specific (DPaint) rather than
universal. That gives good confidence in the diagnosis and the shape of the
fix. It has **not** yet been confirmed to fix the actual DPaint crash on
real hardware/WinUAE - that is the next step.

## Build/install/test

    make clean
    make driver

Copy `build/driver/MintPRINT` to `DEVS:Printers/MintPRINT` and reboot (or
otherwise ensure the old driver segment is unloaded) before testing - a
stale, already-loaded driver segment will not pick up this change.

Print from DPaint as usual. If it still crashes, `T:MintPRINT-driver.log`
may not reflect the very last events (the spool process's log write for a
given event can be interrupted by the same crash it's trying to record), so
also note whether the crash address/exception type changed from
`#8000000A` - a different failure would point at a different remaining bug
rather than this one.

Trace lines are unchanged in wording (still start `MintPRINT: `) since only
*how* they reach disk changed, not their content - the JPEG/PWG traces in
`docs/PRINTER_DEVICE_SPIKE3.md` and `docs/PWG_RASTER.md` still apply as-is.
