#include "jpeg_writer.h"

#define MP_JPEG_SCALE_BITS 10
#define MP_JPEG_SCALE (1L << MP_JPEG_SCALE_BITS)

static const short mp_dct[8][8] = {
    { 362, 362, 362, 362, 362, 362, 362, 362 },
    { 502, 426, 284, 100,-100,-284,-426,-502 },
    { 473, 196,-196,-473,-473,-196, 196, 473 },
    { 426,-100,-502,-284, 284, 502, 100,-426 },
    { 362,-362,-362, 362, 362,-362,-362, 362 },
    { 284,-502, 100, 426,-426,-100, 502,-284 },
    { 196,-473, 473,-196,-196, 473,-473, 196 },
    { 100,-284, 426,-502, 502,-426, 284,-100 }
};

static const unsigned char mp_q_luma[64] = {
     8, 9,10,11,12,13,14,15,
     9,10,11,12,13,14,15,16,
    10,11,12,13,14,15,16,17,
    11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,
    13,14,15,16,17,18,19,20,
    14,15,16,17,18,19,20,21,
    15,16,17,18,19,20,21,22
};

static const unsigned char mp_q_chroma[64] = {
    10,11,12,13,14,15,16,17,
    11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,
    13,14,15,16,17,18,19,20,
    14,15,16,17,18,19,20,21,
    15,16,17,18,19,20,21,22,
    16,17,18,19,20,21,22,23,
    17,18,19,20,21,22,23,24
};

static long mp_round_shift(long v, int bits)
{
    long add = 1L << (bits - 1);
    if (v < 0) return -(((-v) + add) >> bits);
    return (v + add) >> bits;
}

static int mp_abs(int v) { return v < 0 ? -v : v; }

static int mp_category(int v)
{
    unsigned int a = (unsigned int)mp_abs(v);
    int n = 0;
    while (a) { ++n; a >>= 1; }
    return n;
}

static unsigned int mp_amplitude_bits(int v, int size)
{
    if (size == 0) return 0;
    if (v >= 0) return (unsigned int)v;
    return (unsigned int)(v + ((1 << size) - 1));
}

static int mp_write_raw(MPJpegEncoder *e, const unsigned char *p, unsigned long n)
{
    if (e->failed) return 0;
    if (!e->write_fn || e->write_fn(e->write_ctx, p, n) != (long)n) {
        e->failed = 1;
        return 0;
    }
    return 1;
}

static int mp_flush_out(MPJpegEncoder *e)
{
    if (!e->out_used) return !e->failed;
    if (!mp_write_raw(e, e->outbuf, e->out_used)) return 0;
    e->out_used = 0;
    return 1;
}

static int mp_put_byte(MPJpegEncoder *e, unsigned int b)
{
    if (e->failed) return 0;
    e->outbuf[e->out_used++] = (unsigned char)b;
    if (e->out_used == sizeof(e->outbuf)) return mp_flush_out(e);
    return 1;
}

static int mp_put_marker(MPJpegEncoder *e, unsigned int m)
{
    return mp_put_byte(e, 0xff) && mp_put_byte(e, m);
}

static int mp_put_u16(MPJpegEncoder *e, unsigned int v)
{
    return mp_put_byte(e, (v >> 8) & 255) && mp_put_byte(e, v & 255);
}

static int mp_emit_entropy_byte(MPJpegEncoder *e, unsigned int b)
{
    if (!mp_put_byte(e, b)) return 0;
    if (b == 0xff) return mp_put_byte(e, 0x00);
    return 1;
}

static int mp_put_bits(MPJpegEncoder *e, unsigned int bits, int count)
{
    if (count <= 0) return !e->failed;
    bits &= (count == 32) ? 0xffffffffUL : ((1UL << count) - 1UL);
    e->bit_acc = (e->bit_acc << count) | bits;
    e->bit_count += count;
    while (e->bit_count >= 8) {
        unsigned int b = (unsigned int)((e->bit_acc >> (e->bit_count - 8)) & 255UL);
        e->bit_count -= 8;
        if (!mp_emit_entropy_byte(e, b)) return 0;
    }
    return 1;
}

static int mp_flush_bits(MPJpegEncoder *e)
{
    if (e->bit_count > 0) {
        int pad = 8 - e->bit_count;
        unsigned int ones = (1U << pad) - 1U;
        if (!mp_put_bits(e, ones, pad)) return 0;
    }
    e->bit_acc = 0;
    e->bit_count = 0;
    return 1;
}

static int mp_zigzag_index(int pos)
{
    int s, count = 0;
    for (s = 0; s <= 14; ++s) {
        int x_min = s > 7 ? s - 7 : 0;
        int x_max = s < 7 ? s : 7;
        if ((s & 1) == 0) {
            int x;
            for (x = x_min; x <= x_max; ++x) {
                int y = s - x;
                if (count++ == pos) return y * 8 + x;
            }
        } else {
            int x;
            for (x = x_max; x >= x_min; --x) {
                int y = s - x;
                if (count++ == pos) return y * 8 + x;
            }
        }
    }
    return 0;
}

static void mp_fdct(const short *block, short *out)
{
    long tmp[64];
    int y, u, v, x;
    for (y = 0; y < 8; ++y) {
        for (u = 0; u < 8; ++u) {
            long sum = 0;
            for (x = 0; x < 8; ++x)
                sum += (long)mp_dct[u][x] * (long)block[y * 8 + x];
            tmp[y * 8 + u] = mp_round_shift(sum, MP_JPEG_SCALE_BITS);
        }
    }
    for (v = 0; v < 8; ++v) {
        for (u = 0; u < 8; ++u) {
            long sum = 0;
            for (y = 0; y < 8; ++y)
                sum += (long)mp_dct[v][y] * tmp[y * 8 + u];
            out[v * 8 + u] = (short)mp_round_shift(sum, MP_JPEG_SCALE_BITS);
        }
    }
}

static int mp_quantize(int value, int q)
{
    if (value < 0) return -(((-value) + q / 2) / q);
    return (value + q / 2) / q;
}

static int mp_emit_dc(MPJpegEncoder *e, int diff)
{
    int size = mp_category(diff);
    if (size > 11) size = 11;
    if (!mp_put_bits(e, (unsigned int)size, 4)) return 0;
    return mp_put_bits(e, mp_amplitude_bits(diff, size), size);
}

static int mp_ac_code_for(int run, int size)
{
    if (run == 0 && size == 0) return 0;
    if (run == 15 && size == 0) return 1;
    return 2 + run * 10 + (size - 1);
}

static int mp_emit_ac_symbol(MPJpegEncoder *e, int run, int size)
{
    int code = mp_ac_code_for(run, size);
    return mp_put_bits(e, (unsigned int)code, 8);
}

static int mp_encode_block(MPJpegEncoder *e, const short *samples,
                           const unsigned char *qtable, int *dc_pred)
{
    short coeff[64];
    int qcoeff[64];
    int k, run;
    int dc, diff;

    mp_fdct(samples, coeff);
    for (k = 0; k < 64; ++k)
        qcoeff[k] = mp_quantize((int)coeff[k], (int)qtable[k]);

    dc = qcoeff[0];
    diff = dc - *dc_pred;
    *dc_pred = dc;
    if (!mp_emit_dc(e, diff)) return 0;

    run = 0;
    for (k = 1; k < 64; ++k) {
        int idx = mp_zigzag_index(k);
        int v = qcoeff[idx];
        int size;
        if (v == 0) {
            ++run;
            continue;
        }
        while (run >= 16) {
            if (!mp_emit_ac_symbol(e, 15, 0)) return 0;
            run -= 16;
        }
        size = mp_category(v);
        if (size > 10) size = 10;
        if (!mp_emit_ac_symbol(e, run, size)) return 0;
        if (!mp_put_bits(e, mp_amplitude_bits(v, size), size)) return 0;
        run = 0;
    }
    if (run) {
        if (!mp_emit_ac_symbol(e, 0, 0)) return 0;
    }
    return 1;
}

static unsigned char mp_get_rgb(const MPJpegEncoder *e, int row, unsigned long x, int c)
{
    unsigned long sx = x < e->width ? x : (e->width - 1);
    int sr = row;
    if (sr < 0) sr = 0;
    if (sr > 15) sr = 15;
    return e->scratch[((unsigned long)sr * e->width + sx) * 3UL + (unsigned long)c];
}

static short mp_y_from_rgb(int r, int g, int b)
{
    return (short)(((77 * r + 150 * g + 29 * b + 128) >> 8) - 128);
}

static short mp_cb_from_rgb(int r, int g, int b)
{
    return (short)(((-43 * r - 85 * g + 128 * b + 128) >> 8));
}

static short mp_cr_from_rgb(int r, int g, int b)
{
    return (short)(((128 * r - 107 * g - 21 * b + 128) >> 8));
}

static int mp_encode_mcu_row(MPJpegEncoder *e)
{
    unsigned long mx;
    unsigned long mcus = (e->width + 15UL) / 16UL;
    short block[64];
    for (mx = 0; mx < mcus; ++mx) {
        int by, bx, yy, xx;
        for (by = 0; by < 2; ++by) {
            for (bx = 0; bx < 2; ++bx) {
                for (yy = 0; yy < 8; ++yy) {
                    for (xx = 0; xx < 8; ++xx) {
                        unsigned long x = mx * 16UL + (unsigned long)(bx * 8 + xx);
                        int row = by * 8 + yy;
                        int r = mp_get_rgb(e, row, x, 0);
                        int g = mp_get_rgb(e, row, x, 1);
                        int b = mp_get_rgb(e, row, x, 2);
                        block[yy * 8 + xx] = mp_y_from_rgb(r, g, b);
                    }
                }
                if (!mp_encode_block(e, block, mp_q_luma, &e->dc_y)) return 0;
            }
        }

        for (yy = 0; yy < 8; ++yy) {
            for (xx = 0; xx < 8; ++xx) {
                unsigned long x0 = mx * 16UL + (unsigned long)(xx * 2);
                int row0 = yy * 2;
                int r = 0, g = 0, b = 0, dy, dx;
                for (dy = 0; dy < 2; ++dy) {
                    for (dx = 0; dx < 2; ++dx) {
                        r += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 0);
                        g += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 1);
                        b += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 2);
                    }
                }
                r = (r + 2) >> 2;
                g = (g + 2) >> 2;
                b = (b + 2) >> 2;
                block[yy * 8 + xx] = mp_cb_from_rgb(r, g, b);
            }
        }
        if (!mp_encode_block(e, block, mp_q_chroma, &e->dc_cb)) return 0;

        for (yy = 0; yy < 8; ++yy) {
            for (xx = 0; xx < 8; ++xx) {
                unsigned long x0 = mx * 16UL + (unsigned long)(xx * 2);
                int row0 = yy * 2;
                int r = 0, g = 0, b = 0, dy, dx;
                for (dy = 0; dy < 2; ++dy) {
                    for (dx = 0; dx < 2; ++dx) {
                        r += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 0);
                        g += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 1);
                        b += mp_get_rgb(e, row0 + dy, x0 + (unsigned long)dx, 2);
                    }
                }
                r = (r + 2) >> 2;
                g = (g + 2) >> 2;
                b = (b + 2) >> 2;
                block[yy * 8 + xx] = mp_cr_from_rgb(r, g, b);
            }
        }
        if (!mp_encode_block(e, block, mp_q_chroma, &e->dc_cr)) return 0;
    }
    ++e->mcu_rows_done;
    return 1;
}

static int mp_write_headers(MPJpegEncoder *e)
{
    int i, t;
    static const unsigned char jfif[14] = {
        'J','F','I','F',0, 1,1, 1, 1,44, 1,44, 0,0
    };

    if (!mp_put_marker(e, 0xd8)) return 0;
    if (!mp_put_marker(e, 0xe0) || !mp_put_u16(e, 16)) return 0;
    for (i = 0; i < 14; ++i) if (!mp_put_byte(e, jfif[i])) return 0;

    if (!mp_put_marker(e, 0xdb) || !mp_put_u16(e, 132)) return 0;
    for (t = 0; t < 2; ++t) {
        const unsigned char *q = t ? mp_q_chroma : mp_q_luma;
        if (!mp_put_byte(e, (unsigned int)t)) return 0;
        for (i = 0; i < 64; ++i) {
            int idx = mp_zigzag_index(i);
            if (!mp_put_byte(e, q[idx])) return 0;
        }
    }

    if (!mp_put_marker(e, 0xc0) || !mp_put_u16(e, 17)) return 0;
    if (!mp_put_byte(e, 8) || !mp_put_u16(e, (unsigned int)e->height) ||
        !mp_put_u16(e, (unsigned int)e->width) || !mp_put_byte(e, 3)) return 0;
    if (!mp_put_byte(e, 1) || !mp_put_byte(e, 0x22) || !mp_put_byte(e, 0)) return 0;
    if (!mp_put_byte(e, 2) || !mp_put_byte(e, 0x11) || !mp_put_byte(e, 1)) return 0;
    if (!mp_put_byte(e, 3) || !mp_put_byte(e, 0x11) || !mp_put_byte(e, 1)) return 0;

    if (!mp_put_marker(e, 0xc4) || !mp_put_u16(e, 418)) return 0;
    for (t = 0; t < 2; ++t) {
        if (!mp_put_byte(e, (unsigned int)t)) return 0;
        for (i = 1; i <= 16; ++i) if (!mp_put_byte(e, i == 4 ? 12 : 0)) return 0;
        for (i = 0; i < 12; ++i) if (!mp_put_byte(e, i)) return 0;
    }
    for (t = 0; t < 2; ++t) {
        int run, size;
        if (!mp_put_byte(e, 0x10U | (unsigned int)t)) return 0;
        for (i = 1; i <= 16; ++i) if (!mp_put_byte(e, i == 8 ? 162 : 0)) return 0;
        if (!mp_put_byte(e, 0x00) || !mp_put_byte(e, 0xf0)) return 0;
        for (run = 0; run < 16; ++run)
            for (size = 1; size <= 10; ++size)
                if (!mp_put_byte(e, (unsigned int)((run << 4) | size))) return 0;
    }

    if (!mp_put_marker(e, 0xda) || !mp_put_u16(e, 12) || !mp_put_byte(e, 3)) return 0;
    if (!mp_put_byte(e, 1) || !mp_put_byte(e, 0x00)) return 0;
    if (!mp_put_byte(e, 2) || !mp_put_byte(e, 0x11)) return 0;
    if (!mp_put_byte(e, 3) || !mp_put_byte(e, 0x11)) return 0;
    if (!mp_put_byte(e, 0) || !mp_put_byte(e, 63) || !mp_put_byte(e, 0)) return 0;
    return mp_flush_out(e);
}

unsigned long mp_jpeg_scratch_size(unsigned long width)
{
    if (!width || width > 0x05555555UL) return 0;
    return width * 3UL * 16UL;
}

int mp_jpeg_begin(MPJpegEncoder *e, unsigned long width, unsigned long height,
                  unsigned char *scratch, unsigned long scratch_size,
                  MPJpegWriteFn write_fn, void *write_ctx)
{
    unsigned long need;
    unsigned long i;
    if (!e || !width || !height || width > 65535UL || height > 65535UL || !scratch || !write_fn)
        return 0;
    need = mp_jpeg_scratch_size(width);
    if (!need || scratch_size < need) return 0;

    e->width = width;
    e->height = height;
    e->rows_in = 0;
    e->mcu_rows_done = 0;
    e->scratch = scratch;
    e->scratch_size = scratch_size;
    e->write_fn = write_fn;
    e->write_ctx = write_ctx;
    e->out_used = 0;
    e->bit_acc = 0;
    e->bit_count = 0;
    e->dc_y = e->dc_cb = e->dc_cr = 0;
    e->failed = 0;
    for (i = 0; i < need; ++i) scratch[i] = 255;
    return mp_write_headers(e);
}

int mp_jpeg_write_scanline(MPJpegEncoder *e, const unsigned char *rgb)
{
    unsigned long row;
    unsigned long bytes;
    unsigned long i;
    if (!e || e->failed || !rgb || e->rows_in >= e->height) return 0;
    row = e->rows_in & 15UL;
    bytes = e->width * 3UL;
    for (i = 0; i < bytes; ++i) e->scratch[row * bytes + i] = rgb[i];
    ++e->rows_in;
    if ((e->rows_in & 15UL) == 0) {
        if (!mp_encode_mcu_row(e)) return 0;
    }
    return !e->failed;
}

int mp_jpeg_finish(MPJpegEncoder *e)
{
    unsigned long bytes;
    unsigned long row;
    unsigned long i;
    if (!e || e->failed) return 0;
    if (e->rows_in != e->height) return 0;
    bytes = e->width * 3UL;
    row = e->rows_in & 15UL;
    if (row != 0) {
        unsigned long srcrow = row ? (row - 1UL) : 0UL;
        while (row < 16UL) {
            for (i = 0; i < bytes; ++i)
                e->scratch[row * bytes + i] = e->scratch[srcrow * bytes + i];
            ++row;
        }
        if (!mp_encode_mcu_row(e)) return 0;
    }
    if (!mp_flush_bits(e)) return 0;
    if (!mp_flush_out(e)) return 0;
    if (!mp_put_marker(e, 0xd9)) return 0;
    return mp_flush_out(e) && !e->failed;
}
