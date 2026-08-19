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

## Round 2: CreateNewProc() itself turned out to be part of the problem

The first version of this fix still crashed under DPaint, immediately and
before even the trace log file was created - meaning the crash had moved
*earlier*, to inside `mp_spool_ensure_running()` itself, before the spool
process ever got to run a single line of its own code.

The remaining culprit: `CreateNewProc()` is itself a `dos.library` function,
and several of its tags - `NP_CurrentDir`, `NP_HomeDir`, `NP_ConsoleTask`,
`NP_WindowPtr` - default, when left unspecified, to *duplicating the
calling process's own field*. Concretely, that means dereferencing
`pr_CurrentDir` / `pr_ConsoleTask` / `pr_WindowPtr` on whatever
`FindTask(NULL)` returns, cast straight to `struct Process *` with no check
that it actually is one. From a bare Task - exactly DPaint's
background-print case - those fields don't exist, so the "duplicate from
caller" default is itself the unsafe access, deep inside the one `dos.library`
call (`CreateNewProc`) this design had assumed was the safe, documented
exception.

The fix (already in `driver/spool.c`'s `mp_spool_ensure_running()`): pin
every one of those tags explicitly - `NP_CurrentDir`, `NP_Input`,
`NP_CloseInput`, `NP_Output`, `NP_CloseOutput`, `NP_ConsoleTask`,
`NP_WindowPtr` - so `CreateNewProc` never reads anything from the calling
task's process fields at all, regardless of whether the caller is a real
Process or a bare Task.

## Round 3: it isn't dos.library calls at all - it's PRTA_NoIO

After round 2's fix, a proper control test nailed this down: printing from
DPaint to a real stock driver (PostScript, over Serial) **works, no
crash**. Printing from DPaint to MintPRINT still crashes, with the port
(Serial vs `FILE:`) making no difference. That's conclusive: DPaint's own
print code is not broken (it drives PostScript fine, from the same Task),
and this driver has no `dos.library` calls left in its own code
(`driver/spool.c` verified) - so the remaining problem isn't a `dos.library`
call this driver makes at all.

The data points at `DriverTags`' `PRTA_NoIO` flag instead:

| Driver | Caller context | Result |
|---|---|---|
| MintPRINT (`PRTA_NoIO`) | MultiView/GraphicDump (Process) | works |
| MintPRINT (`PRTA_NoIO`) | DPaint (Task) | crashes |
| PostScript (no `PRTA_NoIO`) | DPaint (same Task) | works |

The crash only occurs at the intersection of `PRTA_NoIO` and a Task-context
caller - neither alone reproduces it. `PRTA_NoIO` tells `printer.device` to
skip opening the configured port itself (this driver does its own output
over the network instead) - a genuinely rare flag that takes `printer.device`
down a code path almost no other driver exercises. The likely explanation:
that path, inside `printer.device`'s own ROM, does something
Process-dependent that the normal (non-NoIO) path doesn't - i.e. the same
class of bug as rounds 1-2, just now inside code this project cannot patch.

**Tested and reverted.** Removing `PRTA_NoIO` made things worse rather than
better or neutral: with it off, `printer.device` tried to actually manage
the configured port (Serial or Parallel were tested), and **WinUAE itself
crashed** - a host-level MiniDump, not a guest-side "Software Failure"
alert. That's a different failure class entirely (something in WinUAE's own
device emulation choking on real port I/O, not necessarily anything to do
with the Task/Process theory this driver's own code addresses), and it
doesn't cleanly confirm or refute whether `printer.device`'s NoIO code path
was the original DPaint crash's cause. Since it's strictly worse than the
crash it was meant to test around, `PRTA_NoIO` is back on. This diagnostic
line of investigation is a dead end as tested - the DPaint crash (with
`PRTA_NoIO` on, port setting irrelevant) remains unresolved.

## Round 4: MultiView broke too - this whole approach reverted

A clean install, tested with MultiView (not DPaint), still crashed -
`#80000008` this time, a fourth different exception vector across four
attempts - with `T:MintPRINT-driver.log` completely empty again. MultiView
is a real Process, not a bare Task, and it was solidly confirmed working
*before* `spool.c` existed ("multiview prints, graphicdump prints"). That
breaks the entire premise rounds 1-3 were built on: the spool process has
not been confirmed to start successfully even once, for any caller, since
it was introduced - meaning rounds 2 and 3 were most likely debugging a
Task/Process-specific theory on top of a regression that was never
caller-specific at all.

**`driver/driver_core.c` has been reverted to the last confirmed-working
version** (the one before `spool.c` existed - commit `81f28f9`), restoring
direct `dos.library` calls from the driver's own callbacks exactly as
before this whole investigation started. `driver/spool.c` and
`driver/spool.h` are left in the repository (still built by the Makefile,
just no longer called from `driver_core.c`) rather than deleted, since the
underlying Task/Process reasoning in rounds 1-2 may still be correct and
worth revisiting - but only once re-introduced incrementally, with an
actual confirmed-working test at each step, rather than landed as one large
change and debugged after the fact against a moving, unconfirmed baseline.

## Round 5: re-wired, with a stricter test protocol

The round 4 revert turned out to be chasing an unreliable signal: on
retest, the *reverted* driver (and even `main`, predating this entire
investigation) also crashed identically on the same WinUAE session - a
session that then started working again after a full WinUAE restart (not
just a guest reboot). That points at the emulator having wedged into a bad
state, not a code regression - which means round 4's "MultiView broke too"
evidence, the whole basis for reverting, may itself have been a false
signal rather than proof `spool.c` never worked.

The underlying Task/Process reasoning (rounds 1-2) is still sound,
documented AmigaOS behaviour regardless of what caused the round 3-4
confusion, and the DPaint crash is still unresolved with the reverted
driver - so `driver_core.c` has been **re-wired to use `spool.c` again**,
identical to the round-2 state (including the `CreateNewProc` explicit-tags
fix - `spool.c` itself was never touched during the round-4 revert).

This time, testing goes through one checkpoint before anything else: a
**fresh WinUAE restart** (not just a guest reboot, given what round 4
turned out to be), print via **MultiView only**, and confirm
`T:MintPRINT-driver.log` actually contains `MintPRINT: Init` - proof the
spool process started successfully, which no earlier round ever actually
confirmed. Only once that's a solid yes does DPaint get retried.

## Current status

**Round 5 checkpoint passed.** Confirmed working end-to-end via MultiView
and MintPrint Settings' own Test Print - real print jobs completing
successfully through the spool process (log, job file open/write/close,
and IPP submit all routed through it). This is the first time the spool
process has been confirmed to actually work, for any caller, since it was
introduced.

DPaint has not yet been retried against this build. That remains the
actual point of this whole architecture - see the top of this document -
and is the next test.

## Build/install/test

    make clean
    make driver

Copy `build/driver/MintPRINT` to `DEVS:Printers/MintPRINT` and reboot (or
otherwise ensure the old driver segment is unloaded) before testing - a
stale, already-loaded driver segment will not pick up any driver change.

Trace lines are unchanged in wording (still start `MintPRINT: `) - the
JPEG/PWG traces in `docs/PRINTER_DEVICE_SPIKE3.md` and
`docs/PWG_RASTER.md` still apply as-is.
