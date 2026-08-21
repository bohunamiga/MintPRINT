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
    DEBUG=0
    SIDES=

`DEBUG=0` is the default: no `T:MintPRINT-gui.log` or
`T:MintPRINT-driver.log` is written, and the temporary rendered job is removed
after submission (including a failed submission). `DEBUG=1` enables both logs
and keeps `T:MintPRINT-job.jpg`, `.pwg`, or `.pdf` for diagnosis.

For compatibility, existing `KEEPJOB=0`/`KEEPJOB=1` profiles are still read as
Debug Off/On. MintPrint Settings writes `DEBUG=` when the profile is next saved.

`SIDES=` accepts `one-sided`, `two-sided-long-edge`, or
`two-sided-short-edge`. MintPrint Settings displays One-sided by default and
only offers duplex values confirmed by Query Printer. For a printer that does
not advertise duplex, Settings saves an empty `SIDES=` value so the driver
omits the optional IPP attribute; absence still means one-sided. Old profiles
without a `SIDES=` line preserve the historical request shape. See
`docs/DUPLEX_PRINTING.md`.

Settings are reloaded for each new graphics print, so changing Unit0 does not
require unloading the driver. Replacing `DEVS:Printers/MintPRINT` itself still
requires a reboot before testing a new driver binary.

`config-Unit0.example` contains the same known-good defaults and can be copied
to the ENV:/ENVARC: locations while the Prefs GUI is being wired to this format.
