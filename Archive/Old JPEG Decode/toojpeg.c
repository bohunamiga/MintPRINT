#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h> // For debug output
#include "toojpeg.h"

// Use AmigaOS types
typedef short SHORT;

// Define MIN/MAX macros
#define TOOJPEG_CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define TOOJPEG_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TOOJPEG_MAX(a, b) ((a) > (b) ? (a) : (b))

// Structure to hold the JPEG encoding state
typedef struct {
    void* context;
    toojpeg_write_func write;
    BYTE* pixels;      // Image pixel data
    int width;         // Image width
    int height;        // Image height
    int is_rgb;        // 1 for RGB, 0 for grayscale
    int quality;       // JPEG quality (1-100)
    int bitbuffer;     // Bit buffer for writing bits
    int bitbuffer_len; // Number of bits in buffer
} toojpeg_state;

// Represent a single Huffman code
typedef struct {
    uint16_t code;     // The Huffman code value
    uint8_t numBits;   // Number of valid bits
} BitCode;

// Bitwise output buffer for JPEG encoding
typedef struct {
    int32_t data;     // Bit buffer
    uint8_t numBits;  // Number of valid bits (the right-most bits)
} BitBuffer;

// Constants for JPEG encoding
#define DCTSIZE 8
#define DCTSIZE2 64

// Add this at the file scope, just before the functions
#define CodeWordLimit 2048 // Maximum coefficient value after DCT

// Add these to hold the Huffman tables
static BitCode huffmanLuminanceDC[256];
static BitCode huffmanLuminanceAC[256];
static BitCode huffmanChrominanceDC[256];
static BitCode huffmanChrominanceAC[256];

// Add this for DCT coefficient codewords
static BitCode codewords[2*CodeWordLimit]; // We need positive and negative range
static BitCode* codewordsOffset;

// Initialize Huffman codewords for DCT coefficients
static void initializeCodewords() {
    codewordsOffset = &codewords[CodeWordLimit]; // Allow negative indices
    uint8_t numBits = 1;
    int32_t mask = 1;
    
    for (int16_t value = 1; value < CodeWordLimit; value++) {
        if (value > mask) {
            numBits++;
            mask = (mask << 1) | 1;
        }
        codewordsOffset[-value] = (BitCode){mask - value, numBits};
        codewordsOffset[+value] = (BitCode){value, numBits};
    }
}

// Generate debug output for DCT coefficients
static void debug_dct_block(SHORT* block, const char* label) {
   char msg[128];
    sprintf(msg, "%s DCT Block:\n", label);
   Write(Output(), msg, strlen(msg));
}

// Quantization tables
static const BYTE std_luminance_qt[64] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
};

static const BYTE std_chrominance_qt[64] = {
    17,  18,  24,  47,  99,  99,  99,  99,
    18,  21,  26,  66,  99,  99,  99,  99,
    24,  26,  56,  99,  99,  99,  99,  99,
    47,  66,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99
};

// ZigZag scan pattern
static const BYTE zigzag[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

// Standard Huffman tables
static const BYTE dc_luminance_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const BYTE dc_luminance_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const BYTE ac_luminance_bits[17] = {0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const BYTE ac_luminance_values[162] = {
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

static const BYTE dc_chrominance_bits[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const BYTE dc_chrominance_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const BYTE ac_chrominance_bits[17] = {0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const BYTE ac_chrominance_values[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

// Generate Huffman codes from counts and values
static void generateHuffmanTable(const uint8_t* numCodes, const uint8_t* values, BitCode* result) {
    int i, j;
    
    // Clear all entries
    for (i = 0; i < 256; i++) {
        result[i].code = 0;
        result[i].numBits = 0;
    }
    
    // Process all bitsizes 1 thru 16
    uint16_t code = 0;
    for (i = 0; i < 16; i++) {
        // Count codes for this bitsize (index i corresponds to length i+1)
        uint8_t count = numCodes[i];
        
        // Assign codes to symbols
        for (j = 0; j < count; j++) {
            uint8_t symbol = *values++;
            result[symbol].code = code++;
            result[symbol].numBits = i + 1; // +1 because counts start at bitsize 1
        }
        
        // Shift for next bitsize
        code <<= 1;
    }
}

// Function to initialize a JPEG encoder state
static void toojpeg_init(toojpeg_state* state, void* context, toojpeg_write_func write,
                        BYTE* pixels, int width, int height, int is_rgb, int quality) {
    state->context = context;
    state->write = write;
    state->pixels = pixels;
    state->width = width;
    state->height = height;
    state->is_rgb = is_rgb;
    state->quality = TOOJPEG_MAX(1, TOOJPEG_MIN(100, quality));
    state->bitbuffer = 0;
    state->bitbuffer_len = 0;
}

// Write a single byte to output
static void toojpeg_write_byte(toojpeg_state* state, BYTE b) {
    state->write(state->context, b);
}

// Write a 16-bit word to output (big-endian)
static void toojpeg_write_word(toojpeg_state* state, int w) {
    toojpeg_write_byte(state, (w >> 8) & 0xFF);
    toojpeg_write_byte(state, w & 0xFF);
}

// Write bits to output, handle byte stuffing (FF 00)
static void toojpeg_write_bits(toojpeg_state* state, int bits, int len) {
    // Append the new bits to those bits leftover from previous call(s)
    state->bitbuffer_len += len;
    state->bitbuffer <<= len;
    state->bitbuffer |= bits;

    // Write all "full" bytes
    while (state->bitbuffer_len >= 8) {
        // Extract highest 8 bits
        state->bitbuffer_len -= 8;
        uint8_t oneByte = (state->bitbuffer >> state->bitbuffer_len) & 0xFF;
        state->write(state->context, oneByte);

        // JPEG requires a 0x00 byte after any 0xFF in the data
        if (oneByte == 0xFF) {
            state->write(state->context, 0);
        }
    }
}

// Flush any remaining bits, pad with 1s
static void toojpeg_flush_bits(toojpeg_state* state) {
    if (state->bitbuffer_len > 0) {
        // Pad with 1's to make a complete byte (as per JPEG spec)
        // 0x7F = binary 0111 1111
        toojpeg_write_bits(state, 0x7F, 7);
    }
}

// Convert to fixed point representation for DCT
#define FIX_0_298631336  ((int)2446)  /* FIX(0.298631336) */
#define FIX_0_390180644  ((int)3196)  /* FIX(0.390180644) */
#define FIX_0_541196100  ((int)4433)  /* FIX(0.541196100) */
#define FIX_0_765366865  ((int)6270)  /* FIX(0.765366865) */
#define FIX_0_899976223  ((int)7373)  /* FIX(0.899976223) */
#define FIX_1_175875602  ((int)9633)  /* FIX(1.175875602) */
#define FIX_1_501321110  ((int)12299) /* FIX(1.501321110) */
#define FIX_1_847759065  ((int)15137) /* FIX(1.847759065) */
#define FIX_1_961570560  ((int)16069) /* FIX(1.961570560) */
#define FIX_2_053119869  ((int)16819) /* FIX(2.053119869) */
#define FIX_2_562915447  ((int)20995) /* FIX(2.562915447) */
#define FIX_3_072711026  ((int)25172) /* FIX(3.072711026) */

#define DESCALE(x, n) (((x) + (1 << ((n)-1))) >> (n))
#define CONST_BITS 13
#define PASS1_BITS 2

// Perform forward DCT (Discrete Cosine Transform) on 8x8 block
static void toojpeg_fdct(SHORT* data) {
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int tmp10, tmp11, tmp12, tmp13;
    int z1, z2, z3, z4, z5, z11, z13;
    SHORT* dataptr;
    int ctr;
    
    /* Pass 1: process rows. */
    dataptr = data;
    for (ctr = 0; ctr < DCTSIZE; ctr++) {
        tmp0 = dataptr[0] + dataptr[7];
        tmp7 = dataptr[0] - dataptr[7];
        tmp1 = dataptr[1] + dataptr[6];
        tmp6 = dataptr[1] - dataptr[6];
        tmp2 = dataptr[2] + dataptr[5];
        tmp5 = dataptr[2] - dataptr[5];
        tmp3 = dataptr[3] + dataptr[4];
        tmp4 = dataptr[3] - dataptr[4];
        
        /* Even part */
        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        
        dataptr[0] = (SHORT) ((tmp10 + tmp11) << PASS1_BITS);
        dataptr[4] = (SHORT) ((tmp10 - tmp11) << PASS1_BITS);
        
        z1 = (tmp12 + tmp13) * FIX_0_541196100;
        dataptr[2] = (SHORT) DESCALE(z1 + tmp13 * FIX_0_765366865,
                                    CONST_BITS-PASS1_BITS);
        dataptr[6] = (SHORT) DESCALE(z1 + tmp12 * (-FIX_1_847759065),
                                    CONST_BITS-PASS1_BITS);
        
        /* Odd part */
        tmp10 = tmp4 + tmp5;
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;
        
        z5 = (tmp10 - tmp12) * FIX_0_390180644;
        z2 = tmp10 * FIX_0_541196100 + z5;
        z4 = tmp12 * FIX_1_175875602 + z5;
        z3 = tmp11 * FIX_0_899976223;
        
        z11 = tmp7 + z3;
        z13 = tmp7 - z3;
        
        dataptr[5] = (SHORT) DESCALE(z13 + z2, CONST_BITS-PASS1_BITS);
        dataptr[3] = (SHORT) DESCALE(z11 + z4, CONST_BITS-PASS1_BITS);
        dataptr[1] = (SHORT) DESCALE(z11 - z4, CONST_BITS-PASS1_BITS);
        dataptr[7] = (SHORT) DESCALE(z13 - z2, CONST_BITS-PASS1_BITS);
        
        dataptr += DCTSIZE;
    }
    
    /* Pass 2: process columns. */
    dataptr = data;
    for (ctr = 0; ctr < DCTSIZE; ctr++) {
        tmp0 = dataptr[DCTSIZE*0] + dataptr[DCTSIZE*7];
        tmp7 = dataptr[DCTSIZE*0] - dataptr[DCTSIZE*7];
        tmp1 = dataptr[DCTSIZE*1] + dataptr[DCTSIZE*6];
        tmp6 = dataptr[DCTSIZE*1] - dataptr[DCTSIZE*6];
        tmp2 = dataptr[DCTSIZE*2] + dataptr[DCTSIZE*5];
        tmp5 = dataptr[DCTSIZE*2] - dataptr[DCTSIZE*5];
        tmp3 = dataptr[DCTSIZE*3] + dataptr[DCTSIZE*4];
        tmp4 = dataptr[DCTSIZE*3] - dataptr[DCTSIZE*4];
        
        /* Even part */
        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        
        dataptr[DCTSIZE*0] = (SHORT) DESCALE(tmp10 + tmp11, PASS1_BITS);
        dataptr[DCTSIZE*4] = (SHORT) DESCALE(tmp10 - tmp11, PASS1_BITS);
        
        z1 = (tmp12 + tmp13) * FIX_0_541196100;
        dataptr[DCTSIZE*2] = (SHORT) DESCALE(z1 + tmp13 * FIX_0_765366865,
                                           CONST_BITS+PASS1_BITS);
        dataptr[DCTSIZE*6] = (SHORT) DESCALE(z1 + tmp12 * (-FIX_1_847759065),
                                           CONST_BITS+PASS1_BITS);
        
        /* Odd part */
        tmp10 = tmp4 + tmp5;
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;
        
        z5 = (tmp10 - tmp12) * FIX_0_390180644;
        z2 = tmp10 * FIX_0_541196100 + z5;
        z4 = tmp12 * FIX_1_175875602 + z5;
        z3 = tmp11 * FIX_0_899976223;
        
        z11 = tmp7 + z3;
        z13 = tmp7 - z3;
        
        dataptr[DCTSIZE*5] = (SHORT) DESCALE(z13 + z2, CONST_BITS+PASS1_BITS);
        dataptr[DCTSIZE*3] = (SHORT) DESCALE(z11 + z4, CONST_BITS+PASS1_BITS);
        dataptr[DCTSIZE*1] = (SHORT) DESCALE(z11 - z4, CONST_BITS+PASS1_BITS);
        dataptr[DCTSIZE*7] = (SHORT) DESCALE(z13 - z2, CONST_BITS+PASS1_BITS);
        
        dataptr++;
    }
}

// Write a JPEG marker with data
static void toojpeg_write_marker(toojpeg_state* state, BYTE marker, const BYTE* data, int length) {
    toojpeg_write_byte(state, 0xFF);
    toojpeg_write_byte(state, marker);
    
    if (length > 0) {
        toojpeg_write_word(state, length + 2); // Length includes the 2 bytes for the length field itself
        
        for (int i = 0; i < length; i++) {
            toojpeg_write_byte(state, data[i]);
        }
    }
}

// Write a quantization table
static void toojpeg_write_dqt(toojpeg_state* state, BYTE table_id, const BYTE* table, int quality) {
    BYTE scaled_table[DCTSIZE2];
    int i;
    int quality_factor;
    
    // Scale the quantization table based on quality
    if (quality < 50)
        quality_factor = 5000 / quality;
    else
        quality_factor = 200 - quality * 2;
    
    for (i = 0; i < 64; i++) {
        int val = (table[i] * quality_factor + 50) / 100;
        if (val < 1) val = 1;
        if (val > 255) val = 255;
        scaled_table[i] = (BYTE)val;
    }
    
    // Write DQT marker and data
   //oojpeg_write_byte(state, 0xFF);
   //oojpeg_write_byte(state, 0xDB);
   //oojpeg_write_word(state, 2 + 1 + 64); // Length: 2 (marker) + 1 (id) + 64 (table)
    // Length = 65 bytes (1 byte id + 64 bytes table)
   //oojpeg_write_word(state, 67);
    
    // Write the table identifier
    toojpeg_write_byte(state, table_id);
    
    // Write the quantization table in zigzag order
    for (i = 0; i < 64; i++) {
        toojpeg_write_byte(state, scaled_table[zigzag[i]]);
    }
}

// Write a Huffman table
static void toojpeg_write_dht(toojpeg_state* state, BYTE table_class, BYTE table_id, 
                            const BYTE* bits, const BYTE* values) {
    int i, len = 0;
    
    // Count the number of codes
    for (i = 1; i <= 16; i++) {
        len += bits[i];
    }
    
    // Write the DHT marker
    toojpeg_write_byte(state, 0xFF);
    toojpeg_write_byte(state, 0xC4);
    
    // Length = 16 bytes for bits + len bytes for values + 1 byte for table info + 2 bytes for length field
    toojpeg_write_word(state, 3 + 16 + len);
    
    // Write the table identifier (class in high nibble, id in low nibble)
    toojpeg_write_byte(state, (table_class << 4) | table_id);
    
    // Write number of codes for each length
    for (i = 1; i <= 16; i++) {
        toojpeg_write_byte(state, bits[i]);
    }
    
    // Write the values
    for (i = 0; i < len; i++) {
        toojpeg_write_byte(state, values[i]);
    }
}

// Process an 8x8 block and encode it
static void toojpeg_process_block(toojpeg_state* state, SHORT* block, int component_id, 
    int* last_dc_value, BYTE is_chrominance,
    BitCode* huffmanDC, BitCode* huffmanAC, BitCode* codewords) {
// Make a local copy of the block
SHORT dct_block[64];
memcpy(dct_block, block, 64 * sizeof(SHORT));

// Apply DCT
toojpeg_fdct(dct_block);

// Quantize the coefficients
const BYTE* quant_table = is_chrominance ? std_chrominance_qt : std_luminance_qt;
int quality_factor;

// Scale quantization table based on quality
if (state->quality < 50)
quality_factor = 5000 / state->quality;
else
quality_factor = 200 - state->quality * 2;

for (int i = 0; i < 64; i++) {
int scaled_quant = (quant_table[i] * quality_factor + 50) / 100;
if (scaled_quant < 1) scaled_quant = 1;
if (scaled_quant > 255) scaled_quant = 255;

// Quantize by dividing and rounding
dct_block[i] = (SHORT)((dct_block[i] + (dct_block[i] < 0 ? -scaled_quant/2 : scaled_quant/2)) / scaled_quant);
}

// Encode DC coefficient - difference coding
int dc_value = dct_block[0];
int dc_diff = dc_value - *last_dc_value;
*last_dc_value = dc_value;

if (dc_diff == 0) {
// Zero difference - use special code
toojpeg_write_bits(state, huffmanDC[0].code, huffmanDC[0].numBits);
} else {
// Calculate size (number of bits needed)
int temp = dc_diff;
if (temp < 0) {
temp = -temp;
}

// Count bits needed
int size = 0;
while (temp > 0) {
size++;
temp >>= 1;
}

// Encode the size category
toojpeg_write_bits(state, huffmanDC[size].code, huffmanDC[size].numBits);

// Encode the value within that category
if (size > 0 && dc_diff != 0) {
int bits = dc_diff;
if (dc_diff < 0) {
// For negative values
bits = dc_diff - 1;
}
toojpeg_write_bits(state, bits & ((1 << size) - 1), size);
}
}

// Count trailing zeros
int last_nonzero = 0;
for (int i = 63; i > 0; i--) {
if (dct_block[zigzag[i]] != 0) {
last_nonzero = i;
break;
}
}

// Encode AC coefficients
int run_length = 0;
for (int i = 1; i <= last_nonzero; i++) {
int coef = dct_block[zigzag[i]];

if (coef == 0) {
run_length++;
} else {
// If we have a run of 16+ zeros, write special codes
while (run_length >= 16) {
// 0xF0 is symbol for 16 zeros
toojpeg_write_bits(state, huffmanAC[0xF0].code, huffmanAC[0xF0].numBits);
run_length -= 16;
}

// Calculate size (bits needed)
int temp = coef;
if (temp < 0) {
temp = -temp;
}

// Count bits needed
int size = 0;
while (temp > 0) {
size++;
temp >>= 1;
}

// Encode using the combined run/size code
int symbol = (run_length << 4) | size;
toojpeg_write_bits(state, huffmanAC[symbol].code, huffmanAC[symbol].numBits);

// Encode the coefficient bits
if (size > 0) {
int bits = coef;
if (coef < 0) {
bits = coef - 1;  // Adjust for negative values
}
toojpeg_write_bits(state, bits & ((1 << size) - 1), size);
}

run_length = 0;
}
}

// If the block ends with zeros, write EOB
if (last_nonzero < 63) {
toojpeg_write_bits(state, huffmanAC[0x00].code, huffmanAC[0x00].numBits);
}
}

// Write SOF0 (Start Of Frame) marker
static void toojpeg_write_sof0(toojpeg_state* state) {
    BYTE data[17];
    int offset = 0;
    int width = state->width;
    int height = state->height;
    int components = state->is_rgb ? 3 : 1;
    
    // Precision (8 bits per sample)
    data[offset++] = 8;
    
    // Image height
    data[offset++] = (height >> 8) & 0xFF;
    data[offset++] = height & 0xFF;
    
    // Image width
    data[offset++] = (width >> 8) & 0xFF;
    data[offset++] = width & 0xFF;
    
    // Number of components
    data[offset++] = components;
    
    // For each component
    if (components == 1) {
        // Component ID
        data[offset++] = 1;
        // Sampling factors (1x1)
        data[offset++] = 0x11;
        // Quantization table selector
        data[offset++] = 0;
    } else {
        // Component 1 (Y)
        data[offset++] = 1;
        data[offset++] = 0x11; // 1x1 sampling
        data[offset++] = 0;   // Quantization table 0
        
        // Component 2 (Cb)
        data[offset++] = 2;
        data[offset++] = 0x11; // 1x1 sampling
        data[offset++] = 1;   // Quantization table 1
        
        // Component 3 (Cr)
        data[offset++] = 3;
        data[offset++] = 0x11; // 1x1 sampling
        data[offset++] = 1;   // Quantization table 1
    }
    
    toojpeg_write_marker(state, 0xC0, data, offset);
}

// Write SOS (Start Of Scan) marker
static void toojpeg_write_sos(toojpeg_state* state) {
    BYTE data[14];
    int offset = 0;
    int components = state->is_rgb ? 3 : 1;
    
    // Number of components in scan
    data[offset++] = components;
    
    if (components == 1) {
        // Component ID
        data[offset++] = 1;
        // Huffman table selectors
        data[offset++] = 0x00; // DC table 0, AC table 0
    } else {
        // Component 1 (Y)
        data[offset++] = 1;
        data[offset++] = 0x00; // DC table 0, AC table 0
        
        // Component 2 (Cb)
        data[offset++] = 2;
        data[offset++] = 0x11; // DC table 1, AC table 1
        
        // Component 3 (Cr)
        data[offset++] = 3;
        data[offset++] = 0x11; // DC table 1, AC table 1
    }
    
    // Unused bytes for baseline JPEG
    data[offset++] = 0;  // Spectral selection start
    data[offset++] = 63; // Spectral selection end
    data[offset++] = 0;  // Successive approximation
    
    toojpeg_write_marker(state, 0xDA, data, offset);
}

// Write a JPEG header
static void toojpeg_write_headers(toojpeg_state* state) {
    // SOI marker
    toojpeg_write_byte(state, 0xFF);
    toojpeg_write_byte(state, 0xD8);
    
    // JFIF APP0 marker
    BYTE jfif_app0[14] = {
        'J', 'F', 'I', 'F', 0x00,  // JFIF identifier
        0x01, 0x01,                // Version 1.01
        0x00,                      // Density units (0 = no units)
        0x00, 0x01,                // X density (1)
        0x00, 0x01,                // Y density (1)
        0x00, 0x00                 // Thumbnail size (none)
    };
    toojpeg_write_marker(state, 0xE0, jfif_app0, 14);
    
    // Write quantization tables
    toojpeg_write_dqt(state, 0, std_luminance_qt, state->quality);
    if (state->is_rgb) {
        toojpeg_write_dqt(state, 1, std_chrominance_qt, state->quality);
    }
    
    // Write the frame header
    toojpeg_write_sof0(state);
    
    // Write Huffman tables
    toojpeg_write_dht(state, 0, 0, dc_luminance_bits, dc_luminance_values);
    toojpeg_write_dht(state, 1, 0, ac_luminance_bits, ac_luminance_values);
    
    if (state->is_rgb) {
        toojpeg_write_dht(state, 0, 1, dc_chrominance_bits, dc_chrominance_values);
        toojpeg_write_dht(state, 1, 1, ac_chrominance_bits, ac_chrominance_values);
    }
    
    // Write the scan header
    toojpeg_write_sos(state);
}

// RGB to YCbCr colorspace conversion
static void rgb_to_ycbcr(BYTE r, BYTE g, BYTE b, SHORT* y, SHORT* cb, SHORT* cr) {
    // These match TooJPEG's conversion formulas
    *y  = (SHORT)(+0.299f   * r + 0.587f   * g + 0.114f   * b - 128.0f);
    *cb = (SHORT)(-0.16874f * r - 0.33126f * g + 0.5f     * b + 128.0f);
    *cr = (SHORT)(+0.5f     * r - 0.41869f * g - 0.08131f * b);
}

int toojpeg_write_jpeg(void* context, toojpeg_write_func write, 
    BYTE* pixels, int width, int height, 
    int is_rgb, int quality) {
    toojpeg_state state;
    int x, y, i, j, pos;
    int blocks_width, blocks_height;
    SHORT block[64];
    int last_dc_y = 0, last_dc_cb = 0, last_dc_cr = 0;

    // Huffman tables
    BitCode huffmanLuminanceDC[256];
    BitCode huffmanLuminanceAC[256];
    BitCode huffmanChrominanceDC[256];
    BitCode huffmanChrominanceAC[256];
    initializeCodewords(); // Initialize codewords for DCT coefficients

    // Initialize Huffman tables (skip first element of bits arrays as they start with 0)
    generateHuffmanTable(dc_luminance_bits + 1, dc_luminance_values, huffmanLuminanceDC);
    generateHuffmanTable(ac_luminance_bits + 1, ac_luminance_values, huffmanLuminanceAC);
    generateHuffmanTable(dc_chrominance_bits + 1, dc_chrominance_values, huffmanChrominanceDC);
    generateHuffmanTable(ac_chrominance_bits + 1, ac_chrominance_values, huffmanChrominanceAC);

    // Debug: Print first 5 RGB pixels
    char debug_msg[128];
    sprintf(debug_msg, "First 5 RGB Pixels:\n");
    Write(Output(), debug_msg, strlen(debug_msg));
    for (i = 0; i < 5 && i * 3 < width * height * 3; i++) {
        pos = i * 3; 
        sprintf(debug_msg, "Pixel %d: R=%u G=%u B=%u\n", i, pixels[pos], pixels[pos+1], pixels[pos+2]);
        Write(Output(), debug_msg, strlen(debug_msg));
    }

    // Initialize the encoder state
    toojpeg_init(&state, context, write, pixels, width, height, is_rgb, quality);
    // Reset bit buffer to ensure no leftover bits interfere with headers
    state.bitbuffer = 0;
    state.bitbuffer_len = 0;

    // Write headers at the beginning
    toojpeg_write_headers(&state);

    // Calculate number of 8x8 blocks
    blocks_width = (width + 7) / 8;
    blocks_height = (height + 7) / 8;

    // Process each 8x8 block
    for (y = 0; y < blocks_height; y++) {
        for (x = 0; x < blocks_width; x++) {
            // Y component block
            SHORT y_block[64] = {0};
            
            // Fill Y block data from image
            for (i = 0; i < 8; i++) {
                int block_y = y * 8 + i;
                if (block_y >= height) continue;
                
                for (j = 0; j < 8; j++) {
                    int block_x = x * 8 + j;
                    if (block_x >= width) continue;
                    
                    if (is_rgb) {
                        pos = (block_y * width + block_x) * 3;
                        BYTE r = pixels[pos];
                        BYTE g = pixels[pos + 1];
                        BYTE b = pixels[pos + 2];
                        
                        SHORT y_comp, cb_comp, cr_comp;
                        rgb_to_ycbcr(r, g, b, &y_comp, &cb_comp, &cr_comp);
                        
                        y_block[i * 8 + j] = y_comp;
                    } else {
                        pos = block_y * width + block_x;
                        y_block[i * 8 + j] = (SHORT)(pixels[pos] - 128); // Center around zero
                    }
                }
            }
            
            // Process Y (luminance) component
            toojpeg_process_block(&state, y_block, 0, &last_dc_y, 0, 
                                huffmanLuminanceDC, huffmanLuminanceAC, NULL);
            
            // For color images, process Cb and Cr components
            if (is_rgb) {
                // Cb component block
                SHORT cb_block[64] = {0};
                
                // Fill Cb block with downsampled data
                int cb_sum = 0, count = 0;
                for (i = 0; i < 8; i += 2) {
                    int block_y = y * 8 + i;
                    if (block_y >= height) continue;
                    for (j = 0; j < 8; j += 2) {
                        int block_x = x * 8 + j;
                        if (block_x >= width) continue;
                        pos = (block_y * width + block_x) * 3;
                        BYTE r = pixels[pos];
                        BYTE g = pixels[pos + 1];
                        BYTE b = pixels[pos + 2];
                        SHORT y_comp, cb_comp, cr_comp;
                        rgb_to_ycbcr(r, g, b, &y_comp, &cb_comp, &cr_comp);
                        cb_sum += cb_comp;
                        count++;
                    }
                }
                SHORT cb_avg = count > 0 ? (SHORT)(cb_sum / count) : 0;
                for (i = 0; i < 8; i++) {
                    for (j = 0; j < 8; j++) {
                        cb_block[i * 8 + j] = (i % 2 == 0 && j % 2 == 0) ? cb_avg : 0;
                    }
                }
                
                // Process Cb component
                toojpeg_process_block(&state, cb_block, 1, &last_dc_cb, 1,
                                    huffmanChrominanceDC, huffmanChrominanceAC, NULL);
                
                // Cr component block
                SHORT cr_block[64] = {0};
                
                // Fill Cr block with downsampled data
                int cr_sum = 0;
                count = 0;
                for (i = 0; i < 8; i += 2) {
                    int block_y = y * 8 + i;
                    if (block_y >= height) continue;
                    for (j = 0; j < 8; j += 2) {
                        int block_x = x * 8 + j;
                        if (block_x >= width) continue;
                        pos = (block_y * width + block_x) * 3;
                        BYTE r = pixels[pos];
                        BYTE g = pixels[pos + 1];
                        BYTE b = pixels[pos + 2];
                        SHORT y_comp, cb_comp, cr_comp;
                        rgb_to_ycbcr(r, g, b, &y_comp, &cb_comp, &cr_comp);
                        cr_sum += cr_comp;
                        count++;
                    }
                }
                SHORT cr_avg = count > 0 ? (SHORT)(cr_sum / count) : 0;
                for (i = 0; i < 8; i++) {
                    for (j = 0; j < 8; j++) {
                        cr_block[i * 8 + j] = (i % 2 == 0 && j % 2 == 0) ? cr_avg : 0;
                    }
                }
                
                // Process Cr component
                toojpeg_process_block(&state, cr_block, 2, &last_dc_cr, 1,
                                    huffmanChrominanceDC, huffmanChrominanceAC, NULL);
            }
        }
    }

    // Flush any remaining bits
    toojpeg_flush_bits(&state);

    // Write EOI marker
    toojpeg_write_byte(&state, 0xFF);
    toojpeg_write_byte(&state, 0xD9);

    return 1; // Success
}
