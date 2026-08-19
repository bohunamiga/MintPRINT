# MintPRINT runtime configuration

MintPRINT reads its printer endpoint at the start of every graphics print job.
The live profile is:

    ENV:MintPRINT/Unit0

If that file is absent, the driver falls back to:

    ENVARC:MintPRINT/Unit0

If neither exists, the first proven Brother endpoint remains the built-in
fallback so this patch does not change the known-good print path.

The driver itself has no concept of multiple units - it only ever reads
`Unit0`. MintPrint Settings' Unit dropdown (see `docs/MINTPRINT_PREFS.md`)
manages `Unit1`, `Unit2`, ... as switchable saved profiles for people with
more than one printer, and its **Activate** button is how one of those
becomes the live `Unit0` this driver reads.

## Unit0 format

The file is plain text:

    HOST=192.168.0.51
    PORT=80
    PATH=/ipp/print
    KEEPJOB=1

`KEEPJOB=1` leaves `T:MintPRINT-job.jpg` behind after a successful print for
diagnostics. `KEEPJOB=0` removes the temporary JPEG after IPP succeeds. Failed
jobs always keep the JPEG so the submitted document can be inspected.

Settings are reloaded for each new graphics print, so changing Unit0 does not
require unloading the driver. Replacing `DEVS:Printers/MintPRINT` itself still
requires a reboot before testing a new driver binary.

`config-Unit0.example` contains the same known-good defaults and can be copied
to the ENV:/ENVARC: locations while the Prefs GUI is being wired to this format.
