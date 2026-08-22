# PostScript engine

MintPRINT's fourth output backend is selected with `ENGINE=postscript` in
`ENV:MintPRINT/Unit0` (normally set by MintPrint Settings). It submits the
job as `application/postscript` using the existing one-shot IPP Print-Job
path.

The encoder produces a single-page PostScript Level 2 document. Raster rows
still pass through MintPRINT's bounded-memory JPEG encoder, but the compressed
bytes are placed inside the PostScript program through `ASCII85Decode` and
`DCTDecode`. The printer therefore invokes its PostScript interpreter instead
of its direct IPP JPEG handler. This is important for printers such as the
Samsung C480W that advertise and accept `image/jpeg` yet silently discard the
job.

ASCII85 adds roughly 25 percent to the JPEG stream but keeps the document
safe for older text-oriented PostScript transports. The encoder never buffers
the page: it retains one raster row, the existing JPEG scratch area, four
bytes of ASCII85 state, and a 4 KiB output buffer. That output buffer is
important on classic AmigaOS because it avoids a synchronous spool-process
round trip and DOS write for every five encoded ASCII85 bytes.

When the configured media keyword contains dimensions, `/PageSize` is derived
from that real medium (for example A4) and oriented to match the raster. The
raster keeps its aspect ratio, is fitted if necessary, and is centred on the
sheet. Unknown/custom media falls back to the raster dimensions and configured
DPI.

MintPrint Settings starts Test Print with asynchronous `SendIO()`. The button
is disabled until the request completes, but the Settings window continues to
refresh and process events while a slow PostScript printer handles the job.
The PostScript diagnostic uses an explicit 4.5 by 6 inch portrait dump. The
PostScript writer, rather than `printer.device`, centres that raster on the
configured media. This avoids encoding the otherwise-materialised blank left
margin and keeps the diagnostic portrait without changing how landscape output
from normal applications is handled.

Debug mode retains the generated job as `T:MintPRINT-job.ps`.

`make test-postscript` builds a host-side writer test, creates a real
PostScript file, and asks Ghostscript to interpret it. Hardware testing is
still required because printer firmware can advertise formats inaccurately.
