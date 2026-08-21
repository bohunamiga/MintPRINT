# MintPRINT

IPP/AirPrint printing for classic AmigaOS - print to modern network
printers (JPEG, PostScript, PWG Raster, or PDF; no driver-specific software
on the printer side) straight from Amiga applications, via a real `DEVS:Printers/`
printer.device driver plus a GUI setup tool.

## What's here

- **`driver/`** - `DEVS:Printers/MintPRINT`, the printer.device driver.
  Converts printer.device raster callbacks into a streamed JPEG, PostScript,
  PWG Raster, or PDF document and submits it to the printer's IPP `Print-Job`
  endpoint. See `docs/PRINTER_DEVICE_SPIKE.md` (and its follow-ups) for how
  it was built, and `docs/PWG_RASTER.md`/`docs/DRIVER_SPOOL_PROCESS.md` for
  the two most significant pieces of its design.
- **`src/MintPrintSettings.c`** - MintPrint Settings, the GUI setup/test
  front-end. Discovers printers on the LAN (SSDP + mDNS), queries IPP
  capabilities, supports multiple saved printer profiles (Unit0-7), offers
  to install/update the driver, and can send a test page. See
  `docs/MINTPRINT_PREFS.md`.
- **`windows_ipp_probe.py`** - a small Windows-runnable diagnostic script
  for isolating printer-side vs Amiga-side IPP issues without needing
  Amiga-specific tooling. Useful when reporting a printer MintPRINT
  doesn't work with (see Reporting a problem below).
- **`docs/`** - design notes and build logs for the driver and GUI,
  written as the project went rather than after the fact. The
  **[printer compatibility page](docs/PRINTER_COMPATIBILITY.md)** records
  confirmed hardware, AmigaOS/TCP stack combinations and required settings.
- **`Archive/`, `Binarys/`, `Tools/`** - earlier test programs and
  experiments kept for reference; not part of the current driver/GUI.

## Building

Requires `m68k-amigaos-gcc` (Bebbo's cross-toolchain) on `PATH`, or set
`CROSS=` to a different prefix.

    make gui       # MintPrintSettings
    make driver    # build/driver/MintPRINT
    make release   # both, staged into release/MintPRINT/ ready to distribute
    make clean

`make release` does not generate Workbench icons - add
`MintPrintSettings.info` / `MintPRINT.info` inside `release/MintPRINT/`,
and a drawer icon matching the folder's name in its parent directory,
before distributing.

## Installing

Run `MintPrintSettings` - it detects a missing or out-of-date
`DEVS:Printers/MintPRINT` and offers to install/update it (copying from
next to itself). **Reboot after any driver install or update** - a driver
segment already resident in memory will not pick up a replaced file until
then. Then open `Prefs/Printer`, select `MintPRINT`, and configure your
printer's IP/host, IPP path, and document format in MintPrint Settings.

## Supported document formats

`image/jpeg`, `application/postscript`, `image/pwg-raster`, and
`application/pdf`. Any IPP Everywhere
or AirPrint-certified printer (most network printers from roughly the last
decade) is required to accept PWG Raster, so most printers should already
work with that alone. PostScript and PDF cover older or partially-compliant
IPP printers whose network support fronts an existing office-printer
interpreter and which reject raster formats.

## Reporting a problem

If MintPrint Settings' Query reports that your printer doesn't support any
format MintPRINT can produce, or printing otherwise fails, please
[open an issue](https://github.com/boingball/MintPRINT/issues) and attach
the output of `windows_ipp_probe.py` run against your printer from a
Windows PC on the same network:

    python windows_ipp_probe.py http://<printer-ip>:631/ipp/print

Use the report template on the
[printer compatibility page](docs/PRINTER_COMPATIBILITY.md) so the result can
be added with its AmigaOS version, TCP/IP stack, engine and exact print options.

## Status

Working driver + GUI, actively developed. Not a polished, general-release
product yet - see `docs/` for open issues and design history.

## License

[MIT](LICENSE) - Copyright (c) 2026 Darren Banfi (boingball).
