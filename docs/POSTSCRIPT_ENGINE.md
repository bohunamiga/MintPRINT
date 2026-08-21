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
the page: it retains one raster row, the existing JPEG scratch area, and four
bytes of ASCII85 state. Page dimensions are derived from raster pixels and the
configured DPI, just like the PDF and PWG Raster paths.

Debug mode retains the generated job as `T:MintPRINT-job.ps`.

`make test-postscript` builds a host-side writer test, creates a real
PostScript file, and asks Ghostscript to interpret it. Hardware testing is
still required because printer firmware can advertise formats inaccurately.
