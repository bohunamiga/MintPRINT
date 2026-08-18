# MintPRINT printer.device spike #3 - JPEG + IPP

Spike #2 proved the AmigaOS graphics path and YMCB-to-RGB conversion by
reconstructing a complete page as a PPM. Spike #3 replaces the PPM file with a
small built-in baseline JPEG encoder and then submits that JPEG using IPP
Print-Job.

The first-test endpoint is intentionally the already-proven Brother endpoint:

    192.168.0.51:80/ipp/print

Those defaults live in `driver/ipp_client.h`. A later Prefs patch will move them
to ENV:/ENVARC: instead of compiling them into the spike.

## Memory model

The driver still does not allocate a full RGB page. It keeps:

- one final RGB scanline (`width * 3` bytes);
- sixteen scanlines of JPEG scratch (`width * 3 * 16` bytes);
- a small 4 KiB compressed output buffer.

For the tested 2130-pixel page width, JPEG scratch is about 100 KiB.

## JPEG

The encoder writes a baseline JFIF JPEG at 300 DPI using 4:2:0 chroma
subsampling and integer/fixed-point transforms. It is deliberately simple and
self-contained so MintPRINT does not depend on a third-party JPEG library.

The generated job is left at:

    T:MintPRINT-job.jpg

for this development build. If the printer output looks wrong, copy that file
off the Amiga and inspect exactly what was submitted.

## IPP

After Render phase 4 successfully finalises the JPEG, MintPRINT opens
`bsdsocket.library`, connects to the configured host, and sends an IPP/1.1
Print-Job containing `document-format = image/jpeg`. The request is posted to
`/ipp/print` and the HTTP plus IPP status are written to the trace log.

Expected successful tail:

    MintPRINT: JPEG end rows/expected/failed 1667 1667 0
    MintPRINT: IPP submit 192.168.0.51:80/ipp/print
    MintPRINT: IPP result error/http/status 0 200 0

IPP status 0 is `successful-ok`.

## Build/install/test

    make clean
    make driver

Copy `build/driver/MintPRINT` to `DEVS:Printers/MintPRINT` and **reboot** before
testing; printer driver segments remain resident and replacing the file alone
does not guarantee that the new build is loaded.

Before printing:

    Delete T:MintPRINT-driver.log QUIET
    Delete T:MintPRINT-job.jpg QUIET

Then print the same picture from MultiView. This time the expected end result is
physical paper from the Brother, while `T:MintPRINT-job.jpg` remains available
for comparison.
