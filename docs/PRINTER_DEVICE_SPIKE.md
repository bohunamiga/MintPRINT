# MintPRINT printer.device spike #1

This is an intentionally non-printing AmigaOS printer driver used to prove the
OS integration before the working JPEG/IPP backend is moved into the driver.

## What this spike proves

The classic `printer.device` graphics-driver interface gives a driver a
`Render()` callback. During a graphics dump it supplies the final output width
and height and then supplies raster rows through `struct PrtInfo`, including
Y/M/C/B intensity values and X scaling information.

MintPRINT spike #1 logs those callbacks to:

    T:MintPRINT-driver.log

It **does not send network traffic and does not send anything to a physical
printer**.

The driver is a V44 extended printer driver and sets:

- `PRTA_NoIO = TRUE` so `printer.device` does not need a parallel/serial output
  transport. This is the key architectural fit for the future IPP backend.
- `PRTA_8BitGuns = TRUE` so raster Y/M/C/B intensities are requested as 8-bit
  values, suitable for conversion to RGB/JPEG scanlines later.

## Build

From the repository root:

    make driver

The output should be:

    build/driver/MintPRINT

If the link fails around `_Init`, `_Render`, `_CommandTable`, etc., run:

    make driver-symbols

and keep the output. The printer segment tag uses the classic leading-underscore
Amiga C ABI and this command tells us immediately if a particular GCC build is
using a different symbol convention.

## Install on the Amiga / WinUAE

Keep your normal printer driver safe. This is a development driver.

Copy:

    build/driver/MintPRINT

to:

    DEVS:Printers/MintPRINT

Then open the normal Printer preferences editor and select **MintPRINT** as the
printer driver. Save/use the preference.

Before the first test, remove any old trace:

    Delete T:MintPRINT-driver.log

Now use an ordinary Amiga application that performs a graphics print / raster
print. A paint/image application is ideal for the first test.

After the application reports that printing completed, inspect:

    Type T:MintPRINT-driver.log

## Expected trace

The exact dimensions depend on the application and PrinterGfx preferences, but
we want something shaped like:

    MintPRINT: Init
    MintPRINT: Open
    MintPRINT: Render pre-master special/maxX/maxY 0 4096 6144
    MintPRINT: Render begin width/height/ct 2400 3300 1
    MintPRINT: row=0 sourceWidth=640 scaledWidth=2400 firstYMCB=0,0,0,0
    MintPRINT: row=1650 sourceWidth=640 scaledWidth=2400 firstYMCB=...
    MintPRINT: row=3299 sourceWidth=640 scaledWidth=2400 firstYMCB=...
    MintPRINT: Render end status/rows/expected 0 3300 3300
    MintPRINT: Close

The important success criteria are:

1. the driver loads and `Open` succeeds;
2. `Render begin` reports sensible final page dimensions;
3. raster-row callbacks arrive;
4. `scaledWidth` agrees with the final output width;
5. `rows` agrees with `expected` at the end.

Once that works, spike #2 replaces the row logger with a streaming JPEG encoder
and replaces the harmless close-down with the already-proven IPP Print-Job
backend.
