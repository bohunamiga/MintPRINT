#ifndef MINTPRINT_JPEG_WRITER_H
#define MINTPRINT_JPEG_WRITER_H

typedef long (*MPJpegWriteFn)(void *ctx, const unsigned char *data, unsigned long len);

typedef struct MPJpegEncoder {
    unsigned long width;
    unsigned long height;
    unsigned long rows_in;
    unsigned long mcu_rows_done;
    unsigned char *scratch;
    unsigned long scratch_size;
    MPJpegWriteFn write_fn;
    void *write_ctx;
    unsigned char outbuf[4096];
    unsigned long out_used;
    unsigned long bit_acc;
    int bit_count;
    int dc_y;
    int dc_cb;
    int dc_cr;
    int failed;
} MPJpegEncoder;

unsigned long mp_jpeg_scratch_size(unsigned long width);
int mp_jpeg_begin(MPJpegEncoder *enc, unsigned long width, unsigned long height,
                  unsigned char *scratch, unsigned long scratch_size,
                  MPJpegWriteFn write_fn, void *write_ctx);
int mp_jpeg_write_scanline(MPJpegEncoder *enc, const unsigned char *rgb);
int mp_jpeg_finish(MPJpegEncoder *enc);

#endif
