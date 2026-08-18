# MintPRINT printer.device spike #2 - raster capture

Spike #1 proved that AmigaOS `printer.device` can load MintPRINT and deliver a
complete graphics dump from ordinary applications. Spike #2 keeps networking
disabled and validates the pixel conversion before JPEG/IPP is introduced.

For every graphics print, the driver streams a binary PPM (`P6`) image to:

    SYS:MintPRINT-test.ppm

The file is written one printer row at a time. Only one RGB output row is kept
in memory, so the test does not allocate a full-page bitmap.

## Colour conversion

With `PRTA_8BitGuns`, `printer.device` supplies 8-bit Y/M/C/B intensities in
`pi_ColorInt`. MintPRINT reconstructs RGB from the complementary colour
channels:

    R = 255 - Cyan
    G = 255 - Magenta
    B = 255 - Yellow

The separate `PCMBLACK` value is the black/luminance intensity used by the
classic dithering path; it is intentionally not subtracted a second time.

Horizontal scaling is applied from `pi_ScaleX`, and `pi_xpos` is respected so
left/right margins remain white. Vertical scaling is already represented by
the sequence of raster rows supplied by `printer.device`.

## Build and install

    make clean
    make driver

Copy:

    build/driver/MintPRINT

to:

    DEVS:Printers/MintPRINT

Reboot (or otherwise ensure the old driver segment has been unloaded), select
MintPRINT in Printer prefs, then print a colour image from MultiView or another
application.

Before the test you can remove stale outputs:

    Delete T:MintPRINT-driver.log QUIET
    Delete SYS:MintPRINT-test.ppm QUIET

After printing, inspect the log and copy `SYS:MintPRINT-test.ppm` to a modern
machine for viewing. The PPM should match the application's printed raster in
colour, orientation, aspect ratio and scaling.

For the previously observed 2130 x 1667 page the PPM pixel payload is about
10.2 MiB, so make sure SYS: has enough free disk space. The driver itself still
uses only one ~6 KiB RGB row buffer for that page size.

Expected extra trace lines include:

    MintPRINT: PPM begin width/height/rowbytes 2130 1667 6390
    MintPRINT: PPM end rows/expected/failed 1667 1667 0

Once this image is correct, the next patch replaces the PPM writer with a
streaming JPEG encoder and then hands the completed JPEG to the already-proven
IPP Print-Job transport.
