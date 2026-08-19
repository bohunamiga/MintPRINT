#ifndef MINTPRINT_PDF_WRITER_H
#define MINTPRINT_PDF_WRITER_H

#include "jpeg_writer.h"

/*
 * Minimal streaming single-page PDF encoder.
 *
 * Wraps the existing JPEG encoder's output as-is: PDF's DCTDecode filter
 * embeds a raw baseline JPEG bitstream verbatim, so this writes no new
 * pixel encoder at all, just a small, fixed-shape PDF container around
 * the same byte stream mp_jpeg_*() already produces - one page, one
 * Catalog/Pages/Page/XObject/Contents object set, a cross-reference
 * table, and a trailer.
 *
 * Never buffers the page: rows stream straight through to the JPEG
 * encoder exactly as the JPEG-only path does. The one PDF field that
 * genuinely cannot be known until the image stream finishes - its own
 * byte length, conventionally declared in the object dictionary BEFORE
 * the stream data - is instead written as a separate object appearing
 * AFTER the stream ends (`/Length 6 0 R`, an indirect reference; object 6
 * is a plain integer written once the byte count is actually known).
 * This is standard, valid PDF, and avoids needing to buffer the stream
 * or seek back into the file to patch a length field afterward.
 */

typedef long (*MPPdfWriteFn)(void *ctx, const unsigned char *data, unsigned long len);

typedef struct MPPdfEncoder {
    unsigned long width;
    unsigned long height;
    MPPdfWriteFn write_fn;
    void *write_ctx;
    unsigned long file_offset;     /* running total bytes written so far */
    unsigned long obj_offset[7];   /* [1..6] - byte offset of each object, for xref */
    unsigned long image_stream_bytes;
    MPJpegEncoder jpeg;
    int failed;
} MPPdfEncoder;

unsigned long mp_pdf_scratch_size(unsigned long width);
int mp_pdf_begin(MPPdfEncoder *enc, unsigned long width, unsigned long height,
                 unsigned char *scratch, unsigned long scratch_size,
                 MPPdfWriteFn write_fn, void *write_ctx);
int mp_pdf_write_scanline(MPPdfEncoder *enc, const unsigned char *rgb);
int mp_pdf_finish(MPPdfEncoder *enc);

#endif
