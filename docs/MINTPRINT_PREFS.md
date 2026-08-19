# MintPrint Settings

MintPrint Settings (`src/MintPrintSettings.c`, formerly `IPP-Test16.c` /
"MintPRINT Preferences") is the setup and test front-end for
`DEVS:Printers/MintPRINT`.

Startup behaviour:

- Load `ENV:MintPRINT/Unit0` first.
- Fall back to `ENVARC:MintPRINT/Unit0`.
- Saved media, colour, quality and scaling values are shown before a query.
- If no saved value exists, capability controls show `Not Detected` and are
  ghosted until Query Printer succeeds.
- If `DEVS:Printers/MintPRINT` is missing, offer to install it (see
  "Driver install helper" below).

Layout:

- Query Printer sits beside Printer IP/Host.
- Discover sits directly below Query and searches the LAN for printers.
- Printer Engine offers JPEG and PWG Raster.
- Save sits beside Exit.
- Test Print prints the built-in test page through `printer.device`.

`ENGINE=jpeg` and `ENGINE=pwg-raster` are persisted by the preferences program.
JPEG is still the active driver backend in this GUI-polish pass; PWG Raster is
staged for the dedicated backend patch rather than silently pretending it is
already implemented.

The driver reloads Unit0 at the start of every graphics print. Replacing the
printer driver binary itself still requires a reboot before testing it.

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

## Document format reporting

Query Printer now also requests `document-format-supported` and logs the
printer's full advertised list (e.g. `image/jpeg`, `image/pwg-raster`,
`application/pdf`, ...) to the output area. This is informational: MintPRINT
still only ships a working JPEG encoder in the driver, with PWG Raster
staged. If **Save** is pressed with `Printer Engine` set to a format the
most recent query did not see advertised, a warning is logged (Save still
succeeds - this is a heads-up, not a hard block).

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
capabilities to:

    ENV:MintPRINT/Unit0.cache
    ENVARC:MintPRINT/Unit0.cache

The cache contains the available media/tray mappings, colour modes, quality
levels, scaling choices, detected document formats and other detected IPP
values. Unit0 remains the file that stores the user's selected defaults.

When MintPRINT opens, a cache is loaded only if its HOST, PORT and PATH match
the current Unit0 endpoint. A successful new query replaces both cache files,
so the UI always uses the newest detected capability set.

The status/output area now starts below the Test Print / Save / Exit row and the
preferences window is taller so those controls no longer overlap the log box.
