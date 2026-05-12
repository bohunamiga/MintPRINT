# Amiga IPP Print

Experimental IPP/AirPrint printing tools and GUI experiments for AmigaOS.

This project contains work on querying modern IPP printers from AmigaOS, parsing printer capabilities, handling media/tray mappings, and experimenting with JPEG/PWG print job submission.

## Status

Experimental / work in progress.

Several historical test builds are preserved because printer firmware behaviour changed during development and different versions may be useful for comparison.

## Contents

- `IPP-Test16.c` - later GUI/printing test version
- `IPP-130725.c` - later test version from July 2025
- `ippdump.c` - IPP attribute dump/debug utility
- `ipp-dumper.c` - smaller IPP dumping test utility
- `Old IPP Tests/` - archived older experiments
- `Old JPEG Decode/` - archived JPEG decoding experiments

## Target

Classic AmigaOS using m68k-amigaos-gcc / Bebbo GCC.

## Notes

This repository is being preserved first, cleaned later.
