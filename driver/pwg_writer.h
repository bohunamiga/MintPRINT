#ifndef MINTPRINT_PWG_WRITER_H
#define MINTPRINT_PWG_WRITER_H

/*
 * Minimal streaming PWG Raster (image/pwg-raster) encoder.
 *
 * Writes a "RaS2" stream: one sync word followed by one or more 1796-byte
 * page headers (srgb_8: 8-bit sRGB, chunked/interleaved) and their compressed
 * rows. Each page is still streamed one row at a time.
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
    unsigned long duplex;
    unsigned long tumble;
    unsigned long cross_feed_transform;
    unsigned long feed_transform;
    unsigned char *scratch;
    unsigned long scratch_size;
    MPPwgWriteFn write_fn;
    void *write_ctx;
    int failed;
} MPPwgEncoder;

/* Byte offsets from the start of a 1796-byte page header. The older public
 * constants below retain their first-page/file-relative values (including
 * the four-byte RaS2 sync) for existing callers.
 */
#define MP_PWG_PAGESIZE_Y_HEADER_OFFSET 356UL
#define MP_PWG_HEIGHT_HEADER_OFFSET     376UL
#define MP_PWG_COMPRESSION_HEADER_OFFSET 404UL
#define MP_PWG_ROWCOUNT_HEADER_OFFSET   408UL

/* Byte offsets (from the very start of the first page's file - including the
 * four-byte "RaS2" sync) of PageSizeY and cupsHeight. A caller that grows a
 * page afterwards via mp_pwg_grow() must patch both values once the final
 * height is known; cupsRowCount is a device-banding field and stays zero. */
#define MP_PWG_PAGESIZE_Y_FIELD_OFFSET 360UL
#define MP_PWG_HEIGHT_FIELD_OFFSET   380UL
#define MP_PWG_COMPRESSION_FIELD_OFFSET 408UL
#define MP_PWG_ROWCOUNT_FIELD_OFFSET 412UL

unsigned long mp_pwg_scratch_size(unsigned long width);

/* Resolves PWG 5102.4 Table 9 for a reverse-side bitmap. Unknown values use
 * the standard normal (1,1) coordinate system. */
void mp_pwg_backside_transform(const char *sides, const char *sheet_back,
                               long *cross_feed, long *feed);
void mp_pwg_reverse_rgb_row(unsigned char *rgb, unsigned long width);
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

/* Starts another page in the same PWG Raster stream. write_sync must be true
 * for the first page and false for every later page. */
int mp_pwg_begin_page(MPPwgEncoder *enc,
                      unsigned long width, unsigned long height,
                      unsigned long page_pts_x, unsigned long page_pts_y,
                      unsigned long dpi, int write_sync,
                      int duplex, int tumble,
                      long cross_feed_transform, long feed_transform,
                      unsigned char *scratch, unsigned long scratch_size,
                      MPPwgWriteFn write_fn, void *write_ctx);
int mp_pwg_write_scanline(MPPwgEncoder *enc, const unsigned char *rgb);

/* Encodes a row into enc->scratch without writing it, then writes a previously
 * encoded row respectively. Used to disk-spool compressed backside rows and
 * replay them in reverse vertical order without a page-sized buffer. */
int mp_pwg_encode_scanline(MPPwgEncoder *enc, const unsigned char *rgb,
                           const unsigned char **encoded,
                           unsigned long *encoded_length);
int mp_pwg_write_encoded_scanline(MPPwgEncoder *enc,
                                  const unsigned char *encoded,
                                  unsigned long encoded_length);
int mp_pwg_finish(MPPwgEncoder *enc);

/* Raises the row-count cap mp_pwg_write_scanline() enforces by extra_rows,
 * without touching anything already written (no new header, no reset).
 * For accumulating several strip-printed bands of the same page into one
 * PWG document, cupsHeight and PageSizeY must be patched separately once
 * the true total is known. Fails if the new total exceeds 65535 rows. */
int mp_pwg_grow(MPPwgEncoder *enc, unsigned long extra_rows);

#endif
