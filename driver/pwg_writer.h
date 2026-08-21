#ifndef MINTPRINT_PWG_WRITER_H
#define MINTPRINT_PWG_WRITER_H

/*
 * Minimal streaming PWG Raster (image/pwg-raster) encoder.
 *
 * Writes a single-page "RaS2" stream: sync word, one 1796-byte page
 * header (srgb_8: 8-bit sRGB, chunked/interleaved), then one compressed
 * row at a time as scanlines arrive - never holds more than one row in
 * memory, matching the existing JPEG encoder's streaming shape.
 *
 * The row compression is PWG/CUPS raster's PackBits-style scheme, but
 * deliberately uses only its "repeat run" half (control byte 0-127,
 * meaning "the next pixel repeated (byte+1) times", 1-128): every
 * matching run of identical pixels is compressed normally, and any
 * non-repeating pixel is simply emitted as a trivial run of one. This
 * is a fully valid encoding of the same format - PWG/CUPS raster
 * readers only care that runs decode correctly, not that literal runs
 * (the format's other half, byte 129-255) were used where they could
 * have been - so it reaches full spec conformance without needing that
 * second, more failure-prone code path at all.
 */

typedef long (*MPPwgWriteFn)(void *ctx, const unsigned char *data, unsigned long len);

typedef struct MPPwgEncoder {
    unsigned long width;
    unsigned long height;
    unsigned long bytes_per_line;
    unsigned long rows_written;
    unsigned long page_pts_x;
    unsigned long page_pts_y;
    unsigned long dpi;
    unsigned char *scratch;
    unsigned long scratch_size;
    MPPwgWriteFn write_fn;
    void *write_ctx;
    int failed;
} MPPwgEncoder;

/* Byte offsets (from the very start of the page - i.e. including the
 * 4-byte "RaS2" sync) of PageSizeY, cupsHeight and cupsRowCount. They are
 * written by mp_pwg_begin() using the height known at that time. A caller
 * that grows the page afterwards via mp_pwg_grow() must patch the three
 * values once the final height is known; mp_pwg_grow() only raises the
 * encoder's in-memory row-count cap. */
#define MP_PWG_PAGESIZE_Y_FIELD_OFFSET 360UL
#define MP_PWG_HEIGHT_FIELD_OFFSET   380UL
#define MP_PWG_ROWCOUNT_FIELD_OFFSET 412UL

unsigned long mp_pwg_scratch_size(unsigned long width);
/* page_pts_x/y is the PHYSICAL page size (in 1/72in points) declared in
 * the header's PageSize field - independent of width/height, which are
 * only the pixel raster's own dimensions. Pass 0 for either to fall back
 * to deriving it from width/height/dpi; callers that know the real
 * selected media (e.g. from IPP media=) should pass its actual physical
 * size instead, so this document's own declared page size can never
 * contradict what the surrounding IPP job attributes already told the
 * printer - see pwg_writer.c's file comment.
 *
 * dpi is the capture resolution the raster was rendered at - written into
 * the header's HWResolution field and used for the page_pts_x/y fallback
 * above. Pass 0 to fall back to the previous fixed 300dpi behaviour. */
int mp_pwg_begin(MPPwgEncoder *enc, unsigned long width, unsigned long height,
                 unsigned long page_pts_x, unsigned long page_pts_y,
                 unsigned long dpi,
                 unsigned char *scratch, unsigned long scratch_size,
                 MPPwgWriteFn write_fn, void *write_ctx);
int mp_pwg_write_scanline(MPPwgEncoder *enc, const unsigned char *rgb);
int mp_pwg_finish(MPPwgEncoder *enc);

/* Raises the row-count cap mp_pwg_write_scanline() enforces by extra_rows,
 * without touching anything already written (no new header, no reset).
 * For accumulating several strip-printed bands of the same page into one
 * PWG document: the header's cupsHeight/cupsRowCount fields still say
 * whatever the first band's height was and must be patched separately
 * (including MP_PWG_PAGESIZE_Y_FIELD_OFFSET) once the true total is known.
 * Fails if the new total would exceed 65535 rows. */
int mp_pwg_grow(MPPwgEncoder *enc, unsigned long extra_rows);

#endif
