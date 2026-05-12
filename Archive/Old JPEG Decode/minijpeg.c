#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BYTE unsigned char
#define WORD unsigned short

static FILE *fp;
static int prev_dc[3] = {0, 0, 0};
static unsigned long total_bytes_written = 0;

// --- Bitstream Handling (Adapted from jpec) ---
typedef struct {
    unsigned int bits;
    int len;
    unsigned char *stream;
    int stream_len;
    int stream_capacity;
} jpec_buffer_t;

static void jpec_buffer_init(jpec_buffer_t *b) {
    b->bits = 0;
    b->len = 0;
    b->stream = (unsigned char *)malloc(1024);
    b->stream_len = 0;
    b->stream_capacity = 1024;
}

static void jpec_buffer_write_byte(jpec_buffer_t *b, unsigned char val) {
    if (b->stream_len >= b->stream_capacity) {
        b->stream_capacity *= 2;
        b->stream = (unsigned char *)realloc(b->stream, b->stream_capacity);
    }
    b->stream[b->stream_len++] = val;
    total_bytes_written++;
    static int byte_count = 0;
    if (byte_count < 20) {
        printf("Byte %d: 0x%02X\n", byte_count, val);
        byte_count++;
    }
}

static void jpec_buffer_write_bits(jpec_buffer_t *b, unsigned int val, int len) {
    static int bit_count = 0;
    b->bits = (b->bits << len) | (val & ((1 << len) - 1));
    b->len += len;
    if (bit_count < 100) {
        printf("Writing %d bits: 0x%X\n", len, val);
        bit_count += len;
    }
    while (b->len >= 8) {
        unsigned char out = (b->bits >> (b->len - 8)) & 0xFF;
        jpec_buffer_write_byte(b, out);
        if (out == 0xFF) jpec_buffer_write_byte(b, 0x00);
        b->len -= 8;
    }
}

static void jpec_buffer_flush_bits(jpec_buffer_t *b) {
    if (b->len > 0) {
        int pad = 8 - b->len;
        jpec_buffer_write_bits(b, (1 << pad) - 1, pad);
    }
}

static void write_buffer_to_file(jpec_buffer_t *b) {
    fwrite(b->stream, 1, b->stream_len, fp);
}

static void jpec_buffer_free(jpec_buffer_t *b) {
    free(b->stream);
    b->stream = NULL;
    b->stream_len = 0;
    b->stream_capacity = 0;
}

static void write_word(jpec_buffer_t *b, int val) {
    jpec_buffer_write_byte(b, (val >> 8) & 0xFF);
    jpec_buffer_write_byte(b, val & 0xFF);
}

static void write_marker(jpec_buffer_t *b, int m) {
    jpec_buffer_write_byte(b, 0xFF);
    jpec_buffer_write_byte(b, m);
    printf("Marker: 0xFF%02X\n", m);
}

// --- JPEG Constants ---
static const BYTE zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

static const BYTE std_luminance_qt[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

static const BYTE std_chrominance_qt[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

static const BYTE dc_luminance_bits[17] = { 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const BYTE dc_luminance_val[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const BYTE ac_luminance_bits[17] = { 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D };
static const BYTE ac_luminance_val[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};

static const BYTE dc_chrominance_bits[17] = { 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 };
static const BYTE dc_chrominance_val[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const BYTE ac_chrominance_bits[17] = { 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const BYTE ac_chrominance_val[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};

// --- Huffman Tables (Adapted from jpec) ---
typedef struct {
    uint16_t code;
    uint8_t numBits;
} BitCode;

static BitCode huffman_dc_lum[12];
static BitCode huffman_ac_lum[256];
static BitCode huffman_dc_chroma[12];
static BitCode huffman_ac_chroma[256];

static void generate_huffman_table(const BYTE *bits, const BYTE *values, BitCode *result, int max_symbols) {
    unsigned int code = 0;
    int k = 0;
    for (int numBits = 1; numBits <= 16 && k < max_symbols; numBits++) {
        for (int i = 0; i < bits[numBits - 1] && k < max_symbols; i++) {
            result[values[k]].code = code++;
            result[values[k]].numBits = numBits;
            k++;
        }
        code <<= 1;
    }
}

static void build_huffman_tables() {
    memset(huffman_dc_lum, 0, sizeof(huffman_dc_lum));
    memset(huffman_ac_lum, 0, sizeof(huffman_ac_lum));
    memset(huffman_dc_chroma, 0, sizeof(huffman_dc_chroma));
    memset(huffman_ac_chroma, 0, sizeof(huffman_ac_chroma));
    generate_huffman_table(dc_luminance_bits, dc_luminance_val, huffman_dc_lum, 12);
    generate_huffman_table(ac_luminance_bits, ac_luminance_val, huffman_ac_lum, 256);
    generate_huffman_table(dc_chrominance_bits, dc_chrominance_val, huffman_dc_chroma, 12);
    generate_huffman_table(ac_chrominance_bits, ac_chrominance_val, huffman_ac_chroma, 256);
}

// --- Integer DCT (Adapted from jpec) ---
#define JPEC_DCT_C1  1420
#define JPEC_DCT_C2  1138
#define JPEC_DCT_C3  1776
#define JPEC_DCT_C4   1609
#define JPEC_DCT_C5   565
#define JPEC_DCT_SHIFT 12

static void jpec_dct(int16_t *block) {
    int32_t tmp[64];
    int32_t t0, t1, t2, t3, t4, t5, t6, t7;
    int32_t t10, t11, t12, t13;

    // Rows
    for (int i = 0; i < 8; i++) {
        t0 = block[i * 8 + 0];
        t1 = block[i * 8 + 1] << 8;
        t2 = block[i * 8 + 2];
        t3 = block[i * 8 + 3] << 8;
        t4 = block[i * 8 + 4];
        t5 = block[i * 8 + 5] << 8;
        t6 = block[i * 8 + 6];
        t7 = block[i * 8 + 7] << 8;

        t10 = t0 + t4;
        t11 = t1 + t5;
        t12 = t2 + t6;
        t13 = t3 + t7;

        tmp[i * 8 + 0] = t10 + t12;
        tmp[i * 8 + 4] = t10 - t12;
        tmp[i * 8 + 2] = (t11 * JPEC_DCT_C2 + t13 * JPEC_DCT_C4) >> (JPEC_DCT_SHIFT - 3);
        tmp[i * 8 + 6] = (t11 * JPEC_DCT_C4 - t13 * JPEC_DCT_C2) >> (JPEC_DCT_SHIFT - 3);

        t10 = t0 - t4;
        t11 = t1 - t5;
        t12 = t2 - t6;
        t13 = t3 - t7;

        tmp[i * 8 + 1] = (t10 * JPEC_DCT_C1 + t12 * JPEC_DCT_C3 + t11 * JPEC_DCT_C5) >> (JPEC_DCT_SHIFT - 3);
        tmp[i * 8 + 3] = (t10 * JPEC_DCT_C3 - t12 * JPEC_DCT_C1 + t13 * JPEC_DCT_C5) >> (JPEC_DCT_SHIFT - 3);
        tmp[i * 8 + 5] = (t10 * JPEC_DCT_C5 - t11 * JPEC_DCT_C1 - t13 * JPEC_DCT_C3) >> (JPEC_DCT_SHIFT - 3);
        tmp[i * 8 + 7] = (t11 * JPEC_DCT_C3 - t12 * JPEC_DCT_C5 - t13 * JPEC_DCT_C1) >> (JPEC_DCT_SHIFT - 3);
    }

    // Columns
    for (int i = 0; i < 8; i++) {
        t0 = tmp[0 * 8 + i];
        t1 = tmp[1 * 8 + i] << 8;
        t2 = tmp[2 * 8 + i];
        t3 = tmp[3 * 8 + i] << 8;
        t4 = tmp[4 * 8 + i];
        t5 = tmp[5 * 8 + i] << 8;
        t6 = tmp[6 * 8 + i];
        t7 = tmp[7 * 8 + i] << 8;

        t10 = t0 + t4;
        t11 = t1 + t5;
        t12 = t2 + t6;
        t13 = t3 + t7;

        block[0 * 8 + i] = (t10 + t12) >> 3;
        block[4 * 8 + i] = (t10 - t12) >> 3;
        block[2 * 8 + i] = (t11 * JPEC_DCT_C2 + t13 * JPEC_DCT_C4) >> (JPEC_DCT_SHIFT + 3);
        block[6 * 8 + i] = (t11 * JPEC_DCT_C4 - t13 * JPEC_DCT_C2) >> (JPEC_DCT_SHIFT + 3);

        t10 = t0 - t4;
        t11 = t1 - t5;
        t12 = t2 - t6;
        t13 = t3 - t7;

        block[1 * 8 + i] = (t10 * JPEC_DCT_C1 + t12 * JPEC_DCT_C3 + t11 * JPEC_DCT_C5) >> (JPEC_DCT_SHIFT + 3);
        block[3 * 8 + i] = (t10 * JPEC_DCT_C3 - t12 * JPEC_DCT_C1 + t13 * JPEC_DCT_C5) >> (JPEC_DCT_SHIFT + 3);
        block[5 * 8 + i] = (t10 * JPEC_DCT_C5 - t11 * JPEC_DCT_C1 - t13 * JPEC_DCT_C3) >> (JPEC_DCT_SHIFT + 3);
        block[7 * 8 + i] = (t11 * JPEC_DCT_C3 - t12 * JPEC_DCT_C5 - t13 * JPEC_DCT_C1) >> (JPEC_DCT_SHIFT + 3);
    }
}

// --- JPEG Headers ---
static void write_huffman_tables(jpec_buffer_t *b) {
    write_marker(b, 0xC4);
    int len = 2 + (1 + 16 + 12) + (1 + 16 + 162) + (1 + 16 + 12) + (1 + 16 + 162);
    write_word(b, len);
    printf("DHT length: %d\n", len);

    jpec_buffer_write_byte(b, 0x00);
    for (int i = 1; i <= 16; i++) jpec_buffer_write_byte(b, dc_luminance_bits[i]);
    for (int i = 0; i < 12; i++) jpec_buffer_write_byte(b, dc_luminance_val[i]);

    jpec_buffer_write_byte(b, 0x10);
    for (int i = 1; i <= 16; i++) jpec_buffer_write_byte(b, ac_luminance_bits[i]);
    for (int i = 0; i < 162; i++) jpec_buffer_write_byte(b, ac_luminance_val[i]);

    jpec_buffer_write_byte(b, 0x01);
    for (int i = 1; i <= 16; i++) jpec_buffer_write_byte(b, dc_chrominance_bits[i]);
    for (int i = 0; i < 12; i++) jpec_buffer_write_byte(b, dc_chrominance_val[i]);

    jpec_buffer_write_byte(b, 0x11);
    for (int i = 1; i <= 16; i++) jpec_buffer_write_byte(b, ac_chrominance_bits[i]);
    for (int i = 0; i < 162; i++) jpec_buffer_write_byte(b, ac_chrominance_val[i]);
}

static void write_headers(jpec_buffer_t *b, int w, int h) {
    write_marker(b, 0xD8); // SOI
    write_marker(b, 0xE0); // APP0
    write_word(b, 16);
    jpec_buffer_write_byte(b, 'J'); jpec_buffer_write_byte(b, 'F'); jpec_buffer_write_byte(b, 'I'); 
    jpec_buffer_write_byte(b, 'F'); jpec_buffer_write_byte(b, 0);
    jpec_buffer_write_byte(b, 1); jpec_buffer_write_byte(b, 1); jpec_buffer_write_byte(b, 0);
    write_word(b, 1); write_word(b, 1); jpec_buffer_write_byte(b, 0); jpec_buffer_write_byte(b, 0);

    write_marker(b, 0xDB); // DQT for luminance
    write_word(b, 67);
    jpec_buffer_write_byte(b, 0);
    for (int i = 0; i < 64; i++) jpec_buffer_write_byte(b, std_luminance_qt[i]);

    write_marker(b, 0xDB); // DQT for chrominance
    write_word(b, 67);
    jpec_buffer_write_byte(b, 1);
    for (int i = 0; i < 64; i++) jpec_buffer_write_byte(b, std_chrominance_qt[i]);

    write_marker(b, 0xC0); // SOF0
    write_word(b, 17);
    jpec_buffer_write_byte(b, 8);
    write_word(b, h);
    write_word(b, w);
    jpec_buffer_write_byte(b, 3);
    jpec_buffer_write_byte(b, 1); jpec_buffer_write_byte(b, 0x11); jpec_buffer_write_byte(b, 0);
    jpec_buffer_write_byte(b, 2); jpec_buffer_write_byte(b, 0x11); jpec_buffer_write_byte(b, 1);
    jpec_buffer_write_byte(b, 3); jpec_buffer_write_byte(b, 0x11); jpec_buffer_write_byte(b, 1);

    write_huffman_tables(b);

    write_marker(b, 0xDA); // SOS
    write_word(b, 12);
    jpec_buffer_write_byte(b, 3);
    jpec_buffer_write_byte(b, 1); jpec_buffer_write_byte(b, 0x00);
    jpec_buffer_write_byte(b, 2); jpec_buffer_write_byte(b, 0x11);
    jpec_buffer_write_byte(b, 3); jpec_buffer_write_byte(b, 0x11);
    jpec_buffer_write_byte(b, 0); jpec_buffer_write_byte(b, 63); jpec_buffer_write_byte(b, 0);
}

// --- RGB to YCbCr ---
static void rgb_to_ycbcr_block(const BYTE *rgb, int width, int height, int x, int y, int16_t *Y, int16_t *Cb, int16_t *Cr) {
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
            int pi = x + i, pj = y + j;
            if (pi >= width) pi = width - 1;
            if (pj >= height) pj = height - 1;

            int idx = (pj * width + pi) * 3;
            int r = rgb[idx];
            int g = rgb[idx + 1];
            int b = rgb[idx + 2];

            int yv = (299 * r + 587 * g + 114 * b + 512) >> 10;
            int cbv = (-168 * r - 331 * g + 499 * b + 512) >> 10;
            int crv = (499 * r - 418 * g - 81 * b + 512) >> 10;

            Y[j * 8 + i] = yv - 128;
            Cb[j * 8 + i] = cbv;
            Cr[j * 8 + i] = crv;
        }
    }
    static int first_block = 1;
    if (first_block) {
        printf("Y input to DCT:\n");
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) printf("%4d ", Y[i * 8 + j]);
            printf("\n");
        }
        first_block = 0;
    }
}

// --- Quantization and Encoding (Adapted from jpec) ---
static void quantize(int16_t *block, const BYTE *qtable, int16_t *scaled) {
    for (int i = 0; i < 64; i++) {
        int value = block[zigzag[i]];
        scaled[i] = (value + (value >= 0 ? qtable[i] / 2 : -qtable[i] / 2)) / qtable[i];
        if (scaled[i] > 127) scaled[i] = 127;
        if (scaled[i] < -128) scaled[i] = -128;
    }
}

static int jpec_enc(int16_t *coeffs, BitCode *dc_tab, BitCode *ac_tab, jpec_buffer_t *b, int dc_pred) {
    int dc = coeffs[0];
    int diff = dc - dc_pred;
    int abs_diff = diff < 0 ? -diff : diff;
    int size = 0;
    while (abs_diff) { size++; abs_diff >>= 1; }

    if (size == 0) {
        jpec_buffer_write_bits(b, dc_tab[0].code, dc_tab[0].numBits);
    } else {
        jpec_buffer_write_bits(b, dc_tab[size].code, dc_tab[size].numBits);
        int val = diff < 0 ? (1 << size) + diff : diff;
        jpec_buffer_write_bits(b, val, size);
    }

    int last_nz = 0;
    for (int i = 1; i < 64; i++) {
        if (coeffs[i] != 0) last_nz = i;
    }

    int run = 0;
    for (int i = 1; i <= last_nz; i++) {
        if (coeffs[i] == 0) {
            run++;
            continue;
        }
        while (run >= 16) {
            jpec_buffer_write_bits(b, ac_tab[0xF0].code, ac_tab[0xF0].numBits);
            run -= 16;
        }
        int val = coeffs[i];
        int abs_val = val < 0 ? -val : val;
        int size = 0;
        while (abs_val) { size++; abs_val >>= 1; }
        int symbol = (run << 4) | size;
        jpec_buffer_write_bits(b, ac_tab[symbol].code, ac_tab[symbol].numBits);
        int enc_val = val < 0 ? (1 << size) + val : val;
        jpec_buffer_write_bits(b, enc_val, size);
        run = 0;
    }
    if (last_nz < 63) {
        jpec_buffer_write_bits(b, ac_tab[0x00].code, ac_tab[0x00].numBits);
    }
    return dc;
}

static void encode_block(int16_t *data, const BYTE *qtable, int component, jpec_buffer_t *b) {
    static int block_count = 0;
    int16_t block[64];
    int16_t scaled[64];
    int posNonZero = 0;

    for (int i = 0; i < 64; i++) block[i] = data[i];
    jpec_dct(block);
    quantize(block, qtable, scaled);

    if (block_count == 0 && component == 0) {
        printf("DCT block:\n");
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) printf("%6d ", block[i * 8 + j]);
            printf("\n");
        }
        printf("Quantized block:\n");
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) printf("%4d ", scaled[i * 8 + j]);
            printf("\n");
        }
    }

    BitCode *dc_tab = (component == 0) ? huffman_dc_lum : huffman_dc_chroma;
    BitCode *ac_tab = (component == 0) ? huffman_ac_lum : huffman_ac_chroma;
    prev_dc[component] = jpec_enc(scaled, dc_tab, ac_tab, b, prev_dc[component]);

    block_count++;
}

// --- Main Function ---
int save_minimal_jpeg(const char *filename, const unsigned char *rgb, int width, int height) {
    fp = fopen(filename, "wb");
    if (!fp) return -1;
    if (!rgb) {
        fclose(fp);
        return -1;
    }

    jpec_buffer_t buffer;
    jpec_buffer_init(&buffer);

    build_huffman_tables();
    write_headers(&buffer, width, height);

    int16_t Y[64], Cb[64], Cr[64];
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            rgb_to_ycbcr_block(rgb, width, height, x, y, Y, Cb, Cr);
            encode_block(Y, std_luminance_qt, 0, &buffer);
            encode_block(Cb, std_chrominance_qt, 1, &buffer);
            encode_block(Cr, std_chrominance_qt, 2, &buffer);
        }
    }

    jpec_buffer_flush_bits(&buffer);
    write_marker(&buffer, 0xD9);
    write_buffer_to_file(&buffer);
    printf("Total bytes written: %lu\n", total_bytes_written);
    jpec_buffer_free(&buffer);
    fclose(fp);
    return 0;
}