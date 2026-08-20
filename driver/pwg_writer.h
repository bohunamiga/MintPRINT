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
    unsigned char *scratch;
    unsigned long scratch_size;
    MPPwgWriteFn write_fn;
    void *write_ctx;
    int failed;
} MPPwgEncoder;

unsigned long mp_pwg_scratch_size(unsigned long width);
/* page_pts_x/y is the PHYSICAL page size (in 1/72in points) declared in
 * the header's PageSize field - independent of width/height, which are
 * only the pixel raster's own dimensions. Pass 0 for either to fall back
 * to the previous behaviour of deriving it from width/height at 300dpi;
 * callers that know the real selected media (e.g. from IPP media=) should
 * pass its actual physical size instead, so this document's own declared
 * page size can never contradict what the surrounding IPP job attributes
 * already told the printer - see pwg_writer.c's file comment. */
int mp_pwg_begin(MPPwgEncoder *enc, unsigned long width, unsigned long height,
                 unsigned long page_pts_x, unsigned long page_pts_y,
                 unsigned char *scratch, unsigned long scratch_size,
                 MPPwgWriteFn write_fn, void *write_ctx);
int mp_pwg_write_scanline(MPPwgEncoder *enc, const unsigned char *rgb);
int mp_pwg_finish(MPPwgEncoder *enc);

#endif
