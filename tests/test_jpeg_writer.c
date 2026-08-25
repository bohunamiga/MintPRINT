#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../driver/jpeg_writer.h"

/* Reference IDCT for this test only - NOT part of the encoder.
 *
 * mp_dct_ref[u][x]/1024 is the exact same cosine basis the encoder used
 * before this AAN rewrite (mp_fdct's old direct-matrix implementation,
 * removed from jpeg_writer.c but reproduced here as ground truth). It is
 * numerically confirmed orthonormal (mp_dct_ref^T * mp_dct_ref == very
 * nearly the identity matrix, to the rounding precision of its 1024-scale
 * integers) - see the PR description for the derivation. For an
 * orthonormal 2-D separable transform coeff = M * block * M^T, the exact
 * inverse is block = M^T * coeff * M: this file's mp_ref_idct_block()
 * applies the SAME basis transposed, in the same two-pass row/column
 * shape mp_fdct() itself uses.
 *
 * This gives an inversion path that is independent of - and does not
 * simply trust - the AAN forward transform under test: it reconstructs
 * pixels from mp_fdct()'s dequantised output using different (much
 * simpler, directly verifiable) matrix math, so a scale or sign bug in
 * mp_fdct()/mp_quantize_aan()/mp_fdct_aan_scale[] shows up as a real
 * reconstruction error here rather than being invisible because both
 * sides share the same mistake.
 */
static const double mp_dct_ref[8][8] = {
    { 362, 362, 362, 362, 362, 362, 362, 362 },
    { 502, 426, 284, 100,-100,-284,-426,-502 },
    { 473, 196,-196,-473,-473,-196, 196, 473 },
    { 426,-100,-502,-284, 284, 502, 100,-426 },
    { 362,-362,-362, 362, 362,-362,-362, 362 },
    { 284,-502, 100, 426,-426,-100, 502,-284 },
    { 196,-473, 473,-196,-196, 473,-473, 196 },
    { 100,-284, 426,-502, 502,-426, 284,-100 }
};

static void mp_ref_idct_block(const long *coeff, double *pixel)
{
    double tmp[64];
    int y, x, u, v;

    /* Row pass: undo the forward transform's column pass, i.e. apply the
     * basis transposed along axis v. */
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            double sum = 0.0;
            for (v = 0; v < 8; ++v)
                sum += (mp_dct_ref[v][x] / 1024.0) * (double)coeff[y * 8 + v];
            tmp[y * 8 + x] = sum;
        }
    }
    /* Column pass: undo the forward transform's row pass. */
    for (x = 0; x < 8; ++x) {
        for (y = 0; y < 8; ++y) {
            double sum = 0.0;
            for (u = 0; u < 8; ++u)
                sum += (mp_dct_ref[u][y] / 1024.0) * tmp[u * 8 + x];
            pixel[y * 8 + x] = sum;
        }
    }
}

/* Full round trip through the real encoder math: level-shifted samples ->
 * mp_fdct() (AAN) -> mp_quantize_aan() (the paired compensation) ->
 * dequantise -> mp_ref_idct_block() -> compare against the original
 * samples. qtable mirrors driver/jpeg_writer.c's mp_q_luma (this test has
 * no access to that static table, so it reproduces the same 64 values -
 * any table works for this test's purpose since only the *pairing* with
 * mp_quantize_aan matters, not the specific compression quality). */
static const unsigned char test_qtable[64] = {
     8, 9,10,11,12,13,14,15,
     9,10,11,12,13,14,15,16,
    10,11,12,13,14,15,16,17,
    11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,
    13,14,15,16,17,18,19,20,
    14,15,16,17,18,19,20,21,
    15,16,17,18,19,20,21,22
};

static double mp_round_trip_max_error(const short *block)
{
    long coeff[64];
    long dequant[64];
    double pixel[64];
    double max_err = 0.0;
    int k;

    mp_fdct(block, coeff);
    for (k = 0; k < 64; ++k) {
        int q = mp_quantize_aan(coeff[k], (int)test_qtable[k], mp_fdct_aan_scale[k]);
        dequant[k] = (long)q * (long)test_qtable[k];
    }
    mp_ref_idct_block(dequant, pixel);

    for (k = 0; k < 64; ++k) {
        double err = pixel[k] - (double)block[k];
        if (err < 0) err = -err;
        if (err > max_err) max_err = err;
    }
    return max_err;
}

static void fill_flat(short *block, int value)
{
    int i;
    for (i = 0; i < 64; ++i) block[i] = (short)value;
}

static void fill_gradient(short *block)
{
    int y, x;
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x)
            block[y * 8 + x] = (short)((y * 16 + x * 16) - 128);
}

static void fill_checkerboard(short *block)
{
    int y, x;
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x)
            block[y * 8 + x] = (short)(((y + x) & 1) ? 127 : -128);
}

static void fill_random(short *block, unsigned int *seed)
{
    int i;
    for (i = 0; i < 64; ++i) {
        *seed = *seed * 1103515245u + 12345u;
        block[i] = (short)((int)((*seed >> 16) % 256) - 128);
    }
}

int main(void)
{
    short block[64];
    double err;
    unsigned int seed = 12345u;
    int trial;
    double max_seen = 0.0;

    /* Flat blocks exercise only the DC path, which the AAN butterfly
     * computes via pure addition (no multiply, no rounding) - this is
     * the least ambiguous possible correctness anchor: any scale bug in
     * mp_fdct_aan_scale[]/mp_quantize_aan() big enough to matter would
     * show up here as a large, obvious error, not subtle noise. */
    fill_flat(block, 0);
    err = mp_round_trip_max_error(block);
    printf("flat 0: max error %.3f\n", err);
    assert(err < 1.0);

    fill_flat(block, 127);
    err = mp_round_trip_max_error(block);
    printf("flat 127: max error %.3f\n", err);
    assert(err < 1.0);

    fill_flat(block, -128);
    err = mp_round_trip_max_error(block);
    printf("flat -128: max error %.3f\n", err);
    assert(err < 1.0);

    /* Smooth content (gradient) - low-frequency energy only, should
     * quantise and reconstruct cleanly. */
    fill_gradient(block);
    err = mp_round_trip_max_error(block);
    printf("gradient: max error %.3f\n", err);
    assert(err < 10.0);

    /* Worst case for any 8x8 DCT: a checkerboard is pure Nyquist-frequency
     * energy concentrated in the single highest AC coefficient, which is
     * exactly the coefficient the AAN transform scales most aggressively
     * (mp_fdct_aan_scale[63] = 1247, the smallest entry in the table) -
     * if the compensation math were wrong, this is where it would show. */
    fill_checkerboard(block);
    err = mp_round_trip_max_error(block);
    printf("checkerboard: max error %.3f\n", err);
    assert(err < 20.0);

    /* Broad random-content sweep. */
    for (trial = 0; trial < 500; ++trial) {
        fill_random(block, &seed);
        err = mp_round_trip_max_error(block);
        if (err > max_seen) max_seen = err;
        assert(err < 25.0);
    }
    printf("random sweep (500 blocks): worst-case max error %.3f\n", max_seen);

    puts("jpeg AAN forward DCT tests passed");
    return 0;
}
