// minijpeg.c - Full JPEG Encoder (68030-optimized, baseline, with Huffman encoding)
// Integer-only implementation for 68030 without FPU
#include "minijpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BYTE unsigned char
#define WORD unsigned short
static FILE *fp;
static int bit_buffer = 0;
static int bit_count = 0;
static int prev_dc[3] = {0, 0, 0};

static const BYTE zigzag[64] = {
  0,1,5,6,14,15,27,28, 2,4,7,13,16,26,29,42,
  3,8,12,17,25,30,41,43, 9,11,18,24,31,40,44,53,
 10,19,23,32,39,45,52,54, 20,22,33,38,46,51,55,60,
 21,34,37,47,50,56,59,61, 35,36,48,49,57,58,62,63
};

static const BYTE std_luminance_qt[64] = {
    8,  6,  5,  8, 12, 20, 26, 31,
    6,  6,  7, 10, 13, 29, 30, 28,
    7,  7,  8, 12, 20, 29, 35, 28,
    7,  9, 11, 15, 26, 44, 40, 31,
    9, 11, 19, 28, 34, 55, 52, 39,
    12, 18, 28, 32, 41, 52, 57, 46,
    25, 32, 39, 44, 52, 61, 60, 51,
    36, 46, 48, 49, 56, 50, 52, 50
};

static const BYTE std_chrominance_qt[64] = {
    9,  9,  9, 12, 11, 12, 24, 33,
    9,  9, 11, 14, 15, 17, 33, 50,
    9, 11, 13, 17, 23, 39, 50, 50,
    12, 14, 17, 23, 39, 50, 50, 50,
    11, 15, 23, 39, 50, 50, 50, 50,
    12, 17, 39, 50, 50, 50, 50, 50,
    24, 33, 50, 50, 50, 50, 50, 50,
    33, 50, 50, 50, 50, 50, 50, 50
};

static const BYTE dc_luminance_bits[17] = { 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };

static const BYTE dc_luminance_val[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const BYTE ac_luminance_bits[17] = { 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0 };
static const BYTE ac_luminance_val[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

// Precomputed values for the simplified integer DCT
static const int dct_scale[8][8] = {
    {8, 10, 13, 17, 22, 28, 38, 51},
    {10, 12, 16, 21, 27, 35, 46, 62},
    {13, 16, 22, 29, 38, 51, 68, 91},
    {17, 21, 29, 40, 53, 71, 95, 127},
    {22, 27, 38, 53, 70, 94, 125, 167},
    {28, 35, 51, 71, 94, 127, 169, 226},
    {38, 46, 68, 95, 125, 169, 226, 303},
    {51, 62, 91, 127, 167, 226, 303, 405}
};

static uint16_t dc_code[12];
static uint8_t dc_size[12];
static uint16_t ac_code[256];
static uint8_t ac_size[256];

// Forward declarations
static void encode_block(BYTE *data, const BYTE *qtable, int component);
static void write_bits(int val, int len);
static void flush_bits();
static void write_huffman_tables();

static void build_huffman_tables() {
    int code = 0, k = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < dc_luminance_bits[i]; j++) {
            dc_code[k] = code;
            dc_size[k] = i;
            if (k < 3) { // Debug first few codes
                printf("DC code[%d]: code=0x%x, size=%d\n", k, dc_code[k], dc_size[k]);
            }
            code++;
            k++;
        }
        code <<= 1;
    }
    code = 0; k = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < ac_luminance_bits[i]; j++) {
            ac_code[ac_luminance_val[k]] = code;
            ac_size[ac_luminance_val[k]] = i;
            if (k < 3 || ac_luminance_val[k] == 0x00 || ac_luminance_val[k] == 0xF0) {
                printf("AC code[0x%02x]: code=0x%x, size=%d\n", ac_luminance_val[k], ac_code[ac_luminance_val[k]], ac_size[ac_luminance_val[k]]);
            }
            code++;
            k++;
        }
        code <<= 1;
    }
}

static void write_byte(int val) {
    fputc(val, fp);
    if (val == 0xFF) fputc(0x00, fp);
}

static void write_bits(int val, int len) {
    bit_buffer |= val << (24 - bit_count - len);
    bit_count += len;
    static int byte_count = 0;
    while (bit_count >= 8) {
        BYTE out = (bit_buffer >> 16) & 0xFF;
        if (byte_count < 10) { // Debug first few bytes
            printf("Bitstream byte %d: 0x%02x\n", byte_count, out);
        }
        write_byte(out);
        bit_buffer <<= 8;
        bit_count -= 8;
        byte_count++;
    }
}

static void write_word(int val) {
    write_byte((val >> 8) & 0xFF);
    write_byte(val & 0xFF);
}

static void write_marker(int m) {
    fputc(0xFF, fp);
    fputc(m, fp);
}

static void write_huffman_tables() {
    write_marker(0xC4); // DHT
    write_word(0x01A2); // Correct length: 418 bytes for two DC + two AC tables
    
    // DC Table for Luminance
    write_byte(0x00); // Table ID 0, DC table
    fwrite(dc_luminance_bits + 1, 1, 16, fp);
    fwrite(dc_luminance_val, 1, 12, fp);
    
    // AC Table for Luminance
    write_byte(0x10); // Table ID 0, AC table
    fwrite(ac_luminance_bits + 1, 1, 16, fp);
    fwrite(ac_luminance_val, 1, 162, fp);
    
    // DC Table for Chrominance
    write_byte(0x01); // Table ID 1, DC table
    fwrite(dc_luminance_bits + 1, 1, 16, fp); // Reuse for simplicity
    fwrite(dc_luminance_val, 1, 12, fp);
    
    // AC Table for Chrominance
    write_byte(0x11); // Table ID 1, AC table
    fwrite(ac_luminance_bits + 1, 1, 16, fp); // Reuse for simplicity
    fwrite(ac_luminance_val, 1, 162, fp);
}

static void write_headers(int w, int h) {
    write_marker(0xD8); // SOI
    write_marker(0xE0); // APP0
    write_word(16);
    fwrite("JFIF\0", 1, 5, fp);
    write_byte(1); write_byte(1); write_byte(0);
    write_word(1); write_word(1); write_byte(0); write_byte(0);
    
    // Write quantization tables
    write_marker(0xDB); // DQT
    write_word(67); write_byte(0);  // Luminance table
    fwrite(std_luminance_qt, 1, 64, fp);
    
    write_marker(0xDB); // DQT
    write_word(67); write_byte(1);  // Chrominance table
    fwrite(std_chrominance_qt, 1, 64, fp);
    
    write_marker(0xC0); // SOF0
    write_word(17); write_byte(8);
    write_word(h); write_word(w);
    write_byte(3); // components
    write_byte(1); write_byte(0x11); write_byte(0);
    write_byte(2); write_byte(0x11); write_byte(1);  // Use chroma table (1)
    write_byte(3); write_byte(0x11); write_byte(1);  // Use chroma table (1)
    
    // Write Huffman tables
    write_huffman_tables();
    
    write_marker(0xDA); // SOS
    write_word(12);
    write_byte(3);
    write_byte(1); write_byte(0x00); // Y: DC=0, AC=0
    write_byte(2); write_byte(0x11); // Cb: DC=1, AC=1
    write_byte(3); write_byte(0x11); // Cr: DC=1, AC=1
    write_byte(0); write_byte(63); write_byte(0);
}

static void rgb_to_ycbcr_block(const BYTE *rgb, int width, int height, int x, int y, signed char *Y, signed char *Cb, signed char *Cr) {
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
            int pi = x + i, pj = y + j;
            if (pi >= width) pi = width - 1;
            if (pj >= height) pj = height - 1;
            
            int idx = (pj * width + pi) * 3;
            int r = rgb[idx];
            int g = rgb[idx + 1];
            int b = rgb[idx + 2];
            
            // JPEG standard coefficients (scaled by 1024 for precision)
            int yv  = ( 299 * r + 587 * g + 114 * b + 512) >> 10; // Y
            int cbv = (-168 * r - 331 * g + 499 * b + 512) >> 10; // Cb
            int crv = ( 499 * r - 418 * g - 81  * b + 512) >> 10; // Cr
            
            Y[j * 8 + i]  = yv - 128;  // Center at 0
            Cb[j * 8 + i] = cbv;       // Cb is already centered
            Cr[j * 8 + i] = crv;       // Cr is already centered
        }
    }
}

static const int cos_scale = 16384; // 2^14 for fixed-point precision
static const int cos_table[8] = { // Cosine values scaled by 16384
    16384, 15137, 11585, 6270, 0, -6270, -11585, -15137
};

static void fdct(const signed char *data, short *out) {
    int i;
    int tmp[64];
    const int c1 = 16070; // cos(pi/16) * sqrt(2) * 16384 ≈ 16070
    const int c2 = 15137; // cos(2pi/16) * sqrt(2) * 16384 ≈ 15137
    const int c3 = 13623; // cos(3pi/16) * sqrt(2) * 16384 ≈ 13623
    const int c5 = 9102;  // cos(5pi/16) * sqrt(2) * 16384 ≈ 9102
    const int c6 = 6270;  // cos(6pi/16) * sqrt(2) * 16384 ≈ 6270
    const int c7 = 3196;  // cos(7pi/16) * sqrt(2) * 16384 ≈ 3196

    // Pass 1: process rows
    for (i = 0; i < 8; i++) {
        int x0 = (data[0 + i * 8] + data[7 + i * 8]) << 13; // Revert to original shift
        int x1 = (data[1 + i * 8] + data[6 + i * 8]) << 13;
        int x2 = (data[2 + i * 8] + data[5 + i * 8]) << 13;
        int x3 = (data[3 + i * 8] + data[4 + i * 8]) << 13;

        int x4 = (data[3 + i * 8] - data[4 + i * 8]) << 13;
        int x5 = (data[2 + i * 8] - data[5 + i * 8]) << 13;
        int x6 = (data[1 + i * 8] - data[6 + i * 8]) << 13;
        int x7 = (data[0 + i * 8] - data[7 + i * 8]) << 13;

        int x8 = x0 + x3;
        int x9 = x1 + x2;
        int x10 = x1 - x2;
        int x11 = x0 - x3;

        tmp[i * 8 + 0] = (x8 + x9);
        tmp[i * 8 + 4] = (x8 - x9);

        int t1 = (x10 + x11) * c1 >> 15;
        tmp[i * 8 + 2] = (x11 * c2 + (1 << 14)) >> 15;
        tmp[i * 8 + 6] = (x10 * c3 + (1 << 14)) >> 15;

        int y0 = x4 + x5;
        int y1 = x6 + x7;
        int y2 = x4 + x7;
        int y3 = x5 + x6;

        y0 = (y0 * c5 + (1 << 14)) >> 15;
        y1 = (y1 * c6 + (1 << 14)) >> 15;
        y2 = (y2 * c7 + (1 << 14)) >> 15;
        y3 = (y3 * c1 + (1 << 14)) >> 15;

        tmp[i * 8 + 1] = y0 + y1;
        tmp[i * 8 + 3] = y2 - y3;
        tmp[i * 8 + 5] = y2 + y3;
        tmp[i * 8 + 7] = y0 - y1;
    }

    // Pass 2: process columns
    for (i = 0; i < 8; i++) {
        int x0 = (tmp[i + 0] + tmp[i + 56]);
        int x1 = (tmp[i + 8] + tmp[i + 48]);
        int x2 = (tmp[i + 16] + tmp[i + 40]);
        int x3 = (tmp[i + 24] + tmp[i + 32]);

        int x4 = (tmp[i + 24] - tmp[i + 32]);
        int x5 = (tmp[i + 16] - tmp[i + 40]);
        int x6 = (tmp[i + 8]  - tmp[i + 48]);
        int x7 = (tmp[i + 0]  - tmp[i + 56]);

        int x8 = x0 + x3;
        int x9 = x1 + x2;
        int x10 = x1 - x2;
        int x11 = x0 - x3;

        out[0 * 8 + i] = (short)((x8 + x9 + (1 << 16)) >> 17); // Revert to original shift
        out[4 * 8 + i] = (short)((x8 - x9 + (1 << 16)) >> 17);

        int t1 = (x10 + x11) * c1 >> 15;
        out[2 * 8 + i] = (short)((x11 * c2 + (1 << 14)) >> 15);
        out[6 * 8 + i] = (short)((x10 * c3 + (1 << 14)) >> 15);

        int y0 = x4 + x5;
        int y1 = x6 + x7;
        int y2 = x4 + x7;
        int y3 = x5 + x6;

        y0 = (y0 * c5 + (1 << 14)) >> 15;
        y1 = (y1 * c6 + (1 << 14)) >> 15;
        y2 = (y2 * c7 + (1 << 14)) >> 15;
        y3 = (y3 * c1 + (1 << 14)) >> 15;

        out[1 * 8 + i] = (short)((y0 + y1 + (1 << 14)) >> 15);
        out[3 * 8 + i] = (short)((y2 - y3 + (1 << 14)) >> 15);
        out[5 * 8 + i] = (short)((y2 + y3 + (1 << 14)) >> 15);
        out[7 * 8 + i] = (short)((y0 - y1 + (1 << 14)) >> 15);
    }
}

static void quantize(short *block, const BYTE *qtable) {
    for (int i = 0; i < 64; i++) {
        // Integer division with rounding
        block[i] = (block[i] + (block[i] >= 0 ? qtable[i]/2 : -qtable[i]/2)) / qtable[i];
    }
}

static void zigzag_order(short *block, short *zz) {
    for (int i = 0; i < 64; i++) zz[i] = block[zigzag[i]];
}

static void flush_bits() {
    if (bit_count > 0) {
        BYTE out = (bit_buffer >> 16) & 0xFF;
        write_byte(out);
        bit_buffer <<= 8;
        bit_count -= 8;
        if (bit_count > 0) { // Pad remaining bits with 1s (JPEG standard)
            write_byte((bit_buffer >> 16) | (0xFF >> (8 - bit_count)));
        }
    }
}

static void write_huffman_dc(int diff) {
    int size = 0, val = diff;
    if (diff < 0) val = -val;
    if (val == 0) size = 0;
    else {
        while (val) { size++; val >>= 1; }
    }
    
    static int dc_count = 0;
    if (dc_count == 0) {
        printf("DC: diff=%d, size=%d, code=0x%x, bits=%d\n", diff, size, dc_code[size], dc_size[size]);
    }
    
    write_bits(dc_code[size], dc_size[size]);
    
    if (size > 0) {
        if (diff < 0) {
            diff = (1 << size) - 1 + diff + 1;
        }
        write_bits(diff & ((1 << size) - 1), size);
    }
    dc_count++;
}

static void write_huffman_ac(short *block) {
    int zero_run = 0;
    static int ac_count = 0;
    for (int i = 1; i < 64; i++) {
        if (block[i] == 0) {
            zero_run++;
        } else {
            while (zero_run > 15) {
                write_bits(ac_code[0xF0], ac_size[0xF0]);
                if (ac_count == 0) {
                    printf("AC: run=16, size=0, code=0x%x, bits=%d\n", ac_code[0xF0], ac_size[0xF0]);
                }
                zero_run -= 16;
            }
            int size = 0, temp = block[i];
            if (temp < 0) temp = -temp;
            if (temp == 0) size = 0;
            else {
                while (temp) { size++; temp >>= 1; }
            }
            int symbol = (zero_run << 4) | size;
            write_bits(ac_code[symbol], ac_size[symbol]);
            if (ac_count == 0) {
                printf("AC: run=%d, size=%d, symbol=0x%x, code=0x%x, bits=%d\n", zero_run, size, symbol, ac_code[symbol], ac_size[symbol]);
            }
            
            if (size > 0) {
                if (block[i] < 0) {
                    int val = block[i];
                    val = (1 << size) - 1 + val + 1;
                    write_bits(val & ((1 << size) - 1), size);
                } else {
                    write_bits(block[i] & ((1 << size) - 1), size);
                }
            }
            zero_run = 0;
        }
    }
    if (zero_run > 0) {
        write_bits(ac_code[0x00], ac_size[0x00]);
        if (ac_count == 0) {
            printf("EOB: code=0x%x, bits=%d\n", ac_code[0x00], ac_size[0x00]);
        }
    }
    ac_count++;
}
static void encode_block(BYTE *data, const BYTE *qt, int component) {
    short dct_out[64];
    short zz[64];
    int i;
    static int block_count = 0;

    // Perform DCT
    fdct(data, dct_out);

    // Debugging output (first block only)
    if (component == 0 && block_count == 0) {
        printf("DCT block:\n");
        for (i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                printf("%6d ", dct_out[i*8+j]);
            }
            printf("\n");
        }
    }

    // Quantize DCT coefficients
    quantize(dct_out, qt);
    
    // Debugging output for quantized block (before zigzag)
    if (component == 0 && block_count == 0) {
        printf("Quantized block (before zigzag):\n");
        for (i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                printf("%4d ", dct_out[i*8+j]);
            }
            printf("\n");
        }
    }

    // Reorder using zigzag pattern
    zigzag_order(dct_out, zz);

    // Debugging output (first block only)
    if (component == 0 && block_count == 0) {
        printf("Quantized zigzag block:\n");
        for (i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                printf("%4d ", zz[i*8+j]);
            }
            printf("\n");
        }
    }

    // Encode DC coefficient
    int diff = zz[0] - prev_dc[component];
    write_huffman_dc(diff);
    prev_dc[component] = zz[0];

    // Encode AC coefficients
    write_huffman_ac(zz);
    
    block_count++;
}

int save_minimal_jpeg(const char *filename, const unsigned char *rgb, int width, int height) {
    fp = fopen(filename, "wb");
    if (!fp) return -1;

    build_huffman_tables();
    write_headers(width, height);

    prev_dc[0] = prev_dc[1] = prev_dc[2] = 0;
    int mcu_count = 0;

    signed char Y[64], Cb[64], Cr[64];

    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            rgb_to_ycbcr_block(rgb, width, height, x, y, Y, Cb, Cr);
            
            if (x == 0 && y == 0) {
                printf("RGB block:\n");
                for (int j = 0; j < 8; j++) {
                    for (int i = 0; i < 8; i++) {
                        int idx = ((y + j) * width + (x + i)) * 3;
                        printf("(%3d,%3d,%3d) ", rgb[idx], rgb[idx+1], rgb[idx+2]);
                    }
                    printf("\n");
                }
                printf("Y block:\n");
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++) {
                        printf("%4d ", Y[i * 8 + j]);
                    }
                    printf("\n");
                }
                printf("Cb block:\n");
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++) {
                        printf("%4d ", Cb[i * 8 + j]);
                    }
                    printf("\n");
                }
                printf("Cr block:\n");
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++) {
                        printf("%4d ", Cr[i * 8 + j]);
                    }
                    printf("\n");
                }
            }
            
            encode_block((BYTE*)Y, std_luminance_qt, 0);
            encode_block((BYTE*)Cb, std_chrominance_qt, 1);
            encode_block((BYTE*)Cr, std_chrominance_qt, 2);
            mcu_count++;
        }
    }

    printf("Processed %d MCUs\n", mcu_count);
    flush_bits();
    write_marker(0xD9);
    printf("Wrote EOI\n");
    fclose(fp);
    return 0;
}