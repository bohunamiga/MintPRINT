# MintPRINT Preferences / Test

MintPRINT Preferences is the setup and test front-end for `DEVS:Printers/MintPRINT`.

Startup behaviour:

- Load `ENV:MintPRINT/Unit0` first.
- Fall back to `ENVARC:MintPRINT/Unit0`.
- Saved media, colour, quality and scaling values are shown before a query.
- If no saved value exists, capability controls show `Not Detected` and are
  ghosted until Query Printer succeeds.

Layout:

- Query Printer sits beside Printer IP/Host.
- Printer Engine offers JPEG and PWG Raster.
- Save sits beside Exit.
- Test Print prints the built-in test page through `printer.device`.

`ENGINE=jpeg` and `ENGINE=pwg-raster` are persisted by the preferences program.
JPEG is still the active driver backend in this GUI-polish pass; PWG Raster is
staged for the dedicated backend patch rather than silently pretending it is
already implemented.

The driver reloads Unit0 at the start of every graphics print. Replacing the
printer driver binary itself still requires a reboot before testing it.

## Capability cache

After a successful **Query Printer**, MintPRINT writes the detected printer
capabilities to:

    ENV:MintPRINT/Unit0.cache
    ENVARC:MintPRINT/Unit0.cache

The cache contains the available media/tray mappings, colour modes, quality
levels, scaling choices and other detected IPP values. Unit0 remains the file
that stores the user's selected defaults.

When MintPRINT opens, a cache is loaded only if its HOST, PORT and PATH match
the current Unit0 endpoint. A successful new query replaces both cache files,
so the UI always uses the newest detected capability set.

The status/output area now starts below the Test Print / Save / Exit row and the
preferences window is taller so those controls no longer overlap the log box.
