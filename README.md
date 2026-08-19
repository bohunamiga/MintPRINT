# Amiga IPP Print

Experimental IPP/AirPrint printing tools and GUI experiments for AmigaOS.

This project contains work on querying modern IPP printers from AmigaOS, parsing printer capabilities, handling media/tray mappings, and experimenting with JPEG/PWG print job submission.

`MintPrintSettings.c` builds **MintPrint Settings**, the GUI setup/test tool. It can query a printer by IP, discover printers on the LAN (SSDP), report the document formats a printer advertises, and offer to install `DEVS:Printers/MintPRINT` if it's missing. See `docs/MINTPRINT_PREFS.md` for details.

## Status

Experimental / work in progress.

Several historical test builds are preserved because printer firmware behaviour changed during development and different versions may be useful for comparison.

## Contents

- `MintPrintSettings.c` - MintPrint Settings, the GUI/printing test and configuration front-end (formerly `IPP-Test16.c`)
- `IPP-130725.c` - later test version from July 2025
- `ippdump.c` - IPP attribute dump/debug utility
- `ipp-dumper.c` - smaller IPP dumping test utility
- `Old IPP Tests/` - archived older experiments
- `Old JPEG Decode/` - archived JPEG decoding experiments

## Target

Classic AmigaOS using m68k-amigaos-gcc / Bebbo GCC.

## Notes

This repository is being preserved first, cleaned later.

## Quick Windows IPP Probe

If your Amiga build suddenly stopped printing, you can isolate printer-side vs client-side issues from a Windows machine with `windows_ipp_probe.py`.

Examples:

```bash
python windows_ipp_probe.py http://192.168.0.17:631/ipp/print
python windows_ipp_probe.py http://192.168.0.17:631/ipp/print --print-file test.jpg --mime image/jpeg
```

This checks `Get-Printer-Attributes` and optionally submits a test `Print-Job` with a chosen MIME type.
