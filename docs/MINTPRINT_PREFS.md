# MintPrint Settings

MintPrint Settings (`src/MintPrintSettings.c`, formerly `IPP-Test16.c` /
"MintPRINT Preferences") is the setup and test front-end for
`DEVS:Printers/MintPRINT`.

Startup behaviour:

- Load `ENV:MintPRINT/Unit0` first (the Unit dropdown starts on Unit0).
- Fall back to `ENVARC:MintPRINT/Unit0`.
- Saved media, colour, quality and scaling values are shown before a query.
- If no saved value exists, capability controls show `Not Detected` and are
  ghosted until Query Printer succeeds.
- If `DEVS:Printers/MintPRINT` is missing, offer to install it (see
  "Driver install helper" below).

Layout:

- Unit sits at the top - which saved printer profile is being viewed/edited.
- Query Printer sits beside Printer IP/Host.
- Discover sits directly below Query and searches the LAN for printers.
- Printer Engine offers JPEG, PWG Raster, and PDF.
- Sides defaults to One-sided and only offers capability-confirmed duplex
  modes after Query Printer.
- Save sits beside Exit.
- Test Print prints the built-in test page through `printer.device`.

## Multiple printers (Units)

People with more than one network printer can keep a separate saved profile
per printer:

    ENV:MintPRINT/Unit0, Unit1, Unit2, ... (up to Unit7)

The **Unit** dropdown at the top of the window lists all eight slots, each
labelled from what is actually saved on disk: `Unit1 - Brother HL-L2350DW`
once a make/model is known and saved, plain `Unit1` if the slot has a saved
profile but no make/model yet, or `Unit1 (empty)` if nothing has been saved
there. Picking a different unit reloads the whole form - IP/host, path,
engine, media, cached capabilities, everything - from that unit's saved
files, exactly like **File > Reload Driver Settings** does for the current
one.

**Only Unit0 is what `DEVS:Printers/MintPRINT` actually reads at print
time** - the driver has no concept of "which unit" it was opened as, so
Unit1+ are switchable *saved profiles*, not simultaneously-active printers.
**Activate**, next to the Unit dropdown, is how a different printer becomes
the one that actually prints: it copies the selected unit's saved
`ENV:`/`ENVARC:` config (and cached capabilities, if any) over Unit0's, then
switches the dropdown back to Unit0 so the window reflects what is now
live. Unit0's own previous settings are overwritten by this - if they are
worth keeping, save them to an empty unit slot first. Activate on Unit0
itself is a no-op (it is already active); on a unit with nothing saved yet
it just reports that there is nothing to copy.

`ENGINE=jpeg`, `ENGINE=pwg-raster`, and `ENGINE=pdf` are persisted by the
preferences program and all three are real driver backends:
`DEVS:Printers/MintPRINT` reads Unit0's `ENGINE=` and produces a JPEG, a
PWG Raster (`image/pwg-raster`), or a PDF (`application/pdf`) document
accordingly. See `docs/PWG_RASTER.md` and `docs/PDF_ENGINE.md` for how
each encoder works and what has and hasn't been physically test-printed
yet.

The driver reloads Unit0 at the start of every graphics print. Replacing the
printer driver binary itself still requires a reboot before testing it.

## Duplex

Query Printer reads `sides-supported` plus the IPP multi-document operation
attributes needed by MintPRINT's page-at-a-time driver. The **Sides** selector
is enabled only when both the chosen duplex binding and the complete transport
are advertised. Unsupported or incomplete capability sets remain safely
one-sided. See `docs/DUPLEX_PRINTING.md` for the protocol design and test
matrix.

## LAN printer discovery

Clicking **Discover** runs two passes, each taking about 5 seconds:

1. **SSDP**: a single `M-SEARCH` multicast to `239.255.255.250:1900`,
   catching printers/print servers that answer UPnP discovery.
2. **mDNS**: a DNS PTR query for `_ipp._tcp.local` sent to
   `224.0.0.251:5353` with the "unicast response" bit set, so replies come
   straight back to MintPrint Settings without needing to join the
   multicast group. This is the mechanism most current printers actually
   use to advertise IPP/AirPrint, so it is the pass that matters most in
   practice - SSDP is a bonus for devices that also happen to answer it.

Both passes only look at *which address replied*, not the reply's content
(no SSDP header or DNS record parsing) - that keeps the scan simple and
predictable. Distinct, non-loopback responders from either pass are merged
into one list.

Results appear in a small selection window. Picking one and choosing
**Use Selected** fills in the Printer IP/Host field and runs the same
capability query as the **Query** button (trying the given port, then 631),
so the fetched media/colour/quality/scaling values and the printer's
supported document formats are pulled in immediately - this is where the
printer's actual name/details come from, not the discovery scan itself.

This is a best-effort LAN scan, not a guarantee: a printer that answers
neither SSDP nor mDNS, or that sits behind a router blocking multicast, will
not appear. If nothing is found, enter the IP manually and use **Query** as
before.

## Make and model

Query Printer now also requests `printer-make-and-model` and logs it (e.g.
`Printer: Brother HL-L2350DW series`). A successful **Save** writes it into
the current unit's file as `MODEL=...`, which is what lets the Unit
dropdown show `Unit0 - Brother HL-L2350DW series` instead of a bare
`Unit0`. Until a unit has been queried and saved at least once, its
dropdown entry just shows the unit number (or `(empty)` if nothing has
been saved there at all).

## Document format reporting

Query Printer now also requests `document-format-supported` and logs the
printer's full advertised list (e.g. `image/jpeg`, `image/pwg-raster`,
`application/pdf`, ...) to the output area. This is informational: the driver
only implements three of those itself (JPEG, PWG Raster, and PDF, selected
by `Printer Engine`); anything else in the list is just what the printer
also happens to accept from other clients. If **Save** is pressed with
`Printer Engine` set to a format the most recent query did not see
advertised, a warning is logged (Save still succeeds - this is a
heads-up, not a hard block). If the printer's advertised list contains
**none** of the three formats MintPRINT can produce, a requester points at
filing a GitHub issue with `windows_ipp_probe.py` output attached, since
that printer is not supported yet.

## Driver install helper

MintPrint Settings expects the compiled driver binary to sit next to it as
`PROGDIR:MintPRINT` (i.e. ship `MintPrintSettings` and the driver binary in
the same drawer). On startup, if `DEVS:Printers/MintPRINT` does not exist:

1. If `PROGDIR:MintPRINT` is also missing, a note is logged and startup
   continues normally (nothing to offer to install).
2. Otherwise the user is asked whether to install it. On confirmation the
   driver is copied to `DEVS:Printers/MintPRINT`.
3. On success, the user is asked whether to open Printer preferences now.
   Confirming launches `SYS:Prefs/Printer` (`SystemTags(..., SYS_Asynch,
   TRUE, ...)`) so they can select **MintPRINT** as their printer driver and
   save.

This only ever offers to *install* the driver; selecting it in Printer
preferences and saving remains a manual step the user must do themselves; is
not something Preferences can do automatically.

## Capability cache

After a successful **Query Printer**, MintPRINT writes the detected printer
capabilities for whichever unit is currently selected to:

    ENV:MintPRINT/UnitN.cache
    ENVARC:MintPRINT/UnitN.cache

(`N` is the selected unit's number - `Unit0.cache`, `Unit1.cache`, ...) The
cache contains the available media/tray mappings, colour modes, quality
levels, scaling and sides choices, detected document formats, multi-document
support and other detected IPP values. Each unit's own config file remains what stores its selected
defaults.

When a unit is loaded (at startup, via the Unit dropdown, or File > Reload),
its cache is used only if its HOST, PORT and PATH match that unit's current
endpoint. A successful new query replaces both of that unit's cache files,
so the UI always uses the newest detected capability set.

The status/output area now starts below the Test Print / Save / Exit row and the
preferences window is taller so those controls no longer overlap the log box.
