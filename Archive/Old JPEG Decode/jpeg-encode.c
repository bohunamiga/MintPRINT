/* Amiga JPEG Encoder - Converts IFF/ILBM files to baseline JPEG
   Based on the minimalistic JPEG encoder by Schier Michael
   Adapted for Amiga OS with IFF/ILBM support via direct IFF parsing only
   NOW WITH HAM6 AND HAM8 SUPPORT!
   
   Compile with: m68k-amigaos-gcc -o jpeg-encode jpeg-encode.c -lamiga -lm
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>  // For HAM_KEY and EXTRA_HALFBRITE definitions
#include <workbench/startup.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX(a,b)  (((a)>(b))?(a):(b))
#define MIN(a,b)  (((a)<(b))?(a):(b))
#define CLIP(n, min, max) MIN((MAX((n),(min))), (max))

// IFF chunk IDs - these should match the file format exactly
#define ID_FORM 0x464F524D  // 'FORM' 
#define ID_ILBM 0x494C424D  // 'ILBM'
#define ID_BMHD 0x424D4844  // 'BMHD'
#define ID_CMAP 0x434D4150  // 'CMAP'
#define ID_BODY 0x424F4459  // 'BODY'
#define ID_CAMG 0x43414D47  // 'CAMG' - Amiga viewport modes

// Amiga viewport mode flags - use official NDK definitions
// HAM_KEY and EXTRA_HALFBRITE are already defined in graphics/modeid.h

// HAM pixel types
#define HAM_SET     0  // Set pixel from palette
#define HAM_MODIFY_B 1  // Modify blue component
#define HAM_MODIFY_R 2  // Modify red component  
#define HAM_MODIFY_G 3  // Modify green component

// Byte order conversion functions - FIXED for this system
static inline ULONG ntohl(ULONG x) {
    // The raw bytes are already in the correct order, so don't convert
    return x;
}

static inline UWORD ntohs(UWORD x) {
    // The raw bytes are already in the correct order, so don't convert  
    return x;
}

// Amiga argument parsing
#define TEMPLATE "INPUT/A,OUTPUT/A,QUALITY/N/K"

struct Options {
    STRPTR input;
    STRPTR output;
    LONG *quality;
};

// Stack cookie for adequate stack size
static const char __attribute__((used)) min_stack[] = "$STACK:65536";

// Huffman coding structure
typedef struct __huff_code
{
    int sym_freq[257];     
    int code_len[257];     
    int next[257];         
    int code_len_freq[32]; 
    int sym_sorted[256];   
    int sym_code_len[256]; 
    int sym_code[256];     
} huff_code;

// JPEG data structure
typedef struct __jpeg_data
{
    int width;
    int height;
    int num_pixel;

    // RGB data
    unsigned char* red;
    unsigned char* green;
    unsigned char* blue;

    // YCbCr data
    int* y;
    int* cb;
    int* cr;

    // Sub-sampled chroma
    int* cb_sub;
    int* cr_sub;

    // DCT coefficients
    double* dct_y;
    double* dct_cb;
    double* dct_cr;

    // Quantized DCT coefficients
    int* dct_y_quant;
    int* dct_cb_quant;
    int* dct_cr_quant;

    // Huffman tables
    huff_code luma_dc;
    huff_code luma_ac;
    huff_code chroma_dc;
    huff_code chroma_ac;
} jpeg_data;

// IFF structures
struct IFFHeader {
    ULONG id;        // 'FORM'
    ULONG size;      // File size - 8
    ULONG type;      // 'ILBM'
};

struct IFFChunk {
    ULONG id;        // Chunk ID
    ULONG size;      // Chunk size
};

struct BMHDChunk {
    UWORD w, h;          // Width, height
    WORD x, y;           // Position
    UBYTE nPlanes;       // Number of bitplanes
    UBYTE masking;       // Masking
    UBYTE compression;   // Compression type
    UBYTE pad1;          // Padding
    UWORD transparentColor; // Transparent color
    UBYTE xAspect, yAspect; // Aspect ratio
    WORD pageWidth, pageHeight; // Page size
};

// Quantization tables
int luma_quantizer[] = {
16, 11, 10, 16,  24,  40,  51,  61,
12, 12, 14, 19,  26,  58,  60,  55,
14, 13, 16, 24,  40,  57,  69,  56,
14, 17, 22, 29,  51,  87,  80,  62,
18, 22, 37, 56,  68, 109, 103,  77,
24, 35, 55, 64,  81, 104, 113,  92,
49, 64, 78, 87, 103, 121, 120, 101,
72, 92, 95, 98, 112, 100, 103,  99
};

int chroma_quantizer[] = {
17, 18, 24, 47, 99, 99, 99, 99,
18, 21, 26, 66, 99, 99, 99, 99,
24, 26, 56, 99, 99, 99, 99, 99,
47, 66, 99, 99, 99, 99, 99, 99,
99, 99, 99, 99, 99, 99, 99, 99,
99, 99, 99, 99, 99, 99, 99, 99,
99, 99, 99, 99, 99, 99, 99, 99,
99, 99, 99, 99, 99, 99, 99, 99
};

// Zig-zag scan order
int scan_order[] = {
 0,  1,  8, 16,  9,  2,  3, 10,
17, 24, 32, 25, 18, 11,  4,  5,
12, 19, 26, 33, 40, 48, 41, 34,
27, 20, 13,  6,  7, 14, 21, 28,
35, 42, 49, 56, 57, 50, 43, 36,
29, 22, 15, 23, 30, 37, 44, 51,
58, 59, 52, 45, 38, 31, 39, 46,
53, 60, 61, 54, 47, 55, 62, 63
};

// DCT lookup table
double cos_lookup[8][8];

// File output globals
unsigned char byte_buffer;
int bits_written;

// Function prototypes
int alloc_jpeg_data(jpeg_data* data);
int load_iff_direct(const char *filename, jpeg_data* data);
LONG decompress_rle(UBYTE *src, UBYTE *dest, LONG src_len, LONG dest_len);
UWORD get_pixel_from_planes_fixed(UBYTE **planes, int plane_count, int bytes_per_row, int x, int y, int colors);
void decode_ham_pixels(UBYTE **planes, int plane_count, int bytes_per_row, int width, int height, 
                      UBYTE *colormap, int colors, ULONG camg_mode, jpeg_data* data, int orig_width, int orig_height);
int rgb_to_ycbcr(jpeg_data* data);
void subsample_chroma(jpeg_data* data);
void init_dct_lookup(void);
void dct(int blocks_horiz, int blocks_vert, int in[], double out[]);
void set_quality(int quantizer[], int quality);
void quantize(int num_pixel, double in[], int out[], int quantizer[]);
void zigzag(int num_pixel, int dct[]);
void diff_dc(int num_pixel, int dct_quant[]);
void init_huffman(jpeg_data* data);
void write_file(const char* file_name, jpeg_data* data);

// Allocate memory for JPEG data structures
int alloc_jpeg_data(jpeg_data* data)
{
    data->red = AllocVec(data->num_pixel * sizeof(unsigned char), MEMF_ANY);
    data->green = AllocVec(data->num_pixel * sizeof(unsigned char), MEMF_ANY);
    data->blue = AllocVec(data->num_pixel * sizeof(unsigned char), MEMF_ANY);

    data->y = AllocVec(data->num_pixel * sizeof(int), MEMF_ANY);
    data->cb = AllocVec(data->num_pixel * sizeof(int), MEMF_ANY);
    data->cr = AllocVec(data->num_pixel * sizeof(int), MEMF_ANY);

    data->cb_sub = AllocVec(data->num_pixel/4 * sizeof(int), MEMF_ANY);
    data->cr_sub = AllocVec(data->num_pixel/4 * sizeof(int), MEMF_ANY);

    data->dct_y = AllocVec(data->num_pixel * sizeof(double), MEMF_ANY);
    data->dct_cb = AllocVec(data->num_pixel/4 * sizeof(double), MEMF_ANY);
    data->dct_cr = AllocVec(data->num_pixel/4 * sizeof(double), MEMF_ANY);

    data->dct_y_quant = AllocVec(data->num_pixel * sizeof(int), MEMF_ANY);
    data->dct_cb_quant = AllocVec(data->num_pixel/4 * sizeof(int), MEMF_ANY);
    data->dct_cr_quant = AllocVec(data->num_pixel/4 * sizeof(int), MEMF_ANY);

    if (!data->red || !data->green || !data->blue || !data->y || !data->cb || !data->cr ||
        !data->cb_sub || !data->cr_sub || !data->dct_y || !data->dct_cb || !data->dct_cr ||
        !data->dct_y_quant || !data->dct_cb_quant || !data->dct_cr_quant)
    {
        printf("Could not allocate enough memory for image processing\n");
        return -1;
    }
    return 0;
}

// Main IFF loading function - only uses direct method
int load_iff_to_rgb(const char *filename, jpeg_data* data)
{
    return load_iff_direct(filename, data);
}

// FIXED: RLE decompression for IFF BODY data with safety checks
LONG decompress_rle(UBYTE *src, UBYTE *dest, LONG src_len, LONG dest_len)
{
    LONG src_pos = 0;
    LONG dest_pos = 0;
    int safety_counter = 0;
    
    printf("      RLE: src_len=%ld, dest_len=%ld\n", src_len, dest_len);
    
    while (src_pos < src_len && dest_pos < dest_len && safety_counter < 1000) {
        safety_counter++;
        
        BYTE count = (BYTE)src[src_pos++];
        
        if (count >= 0) {
            // Literal run: copy (count + 1) bytes
            count++;
            printf("      Literal: %d bytes\n", count);
            while (count-- && src_pos < src_len && dest_pos < dest_len) {
                dest[dest_pos++] = src[src_pos++];
            }
        } else if (count != -128) {
            // Repeat run: repeat next byte (-count + 1) times  
            count = -count + 1;
            if (src_pos < src_len) {
                UBYTE repeat_byte = src[src_pos++];
                printf("      Repeat: 0x%02X x %d\n", repeat_byte, count);
                while (count-- && dest_pos < dest_len) {
                    dest[dest_pos++] = repeat_byte;
                }
            }
        }
        // count == -128 is a no-op
    }
    
    printf("      RLE done: %ld bytes out, safety=%d\n", dest_pos, safety_counter);
    return dest_pos;
}

// FIXED: Read pixel index from decompressed planar data with correct bit ordering
UWORD get_pixel_from_planes_fixed(UBYTE **planes, int plane_count, int bytes_per_row, int x, int y, int colors)
{
    UWORD result = 0;
    int byte_offset = y * bytes_per_row + (x >> 3);
    
    // Use MSB ordering (standard for IFF)
    int bit_mask = 0x80 >> (x & 7);
    
    // Only debug a few pixels to avoid spam
    if (x < 3 && y < 2) {
        printf("Pixel(%d,%d): byte_offset=%d, bit_mask=0x%02X -> ", x, y, byte_offset, bit_mask);
    }
    
    int max_planes = (colors > 0) ? (int)ceil(log2(colors)) : 1;
    for (int plane = 0; plane < MIN(plane_count, max_planes); plane++) {
        if (planes[plane] && byte_offset < bytes_per_row * y + bytes_per_row) {
            UBYTE byte_val = planes[plane][byte_offset];
            BOOL bit_set = (byte_val & bit_mask) != 0;
            
            if (bit_set) {
                result |= (1 << plane);
            }
        }
    }
    
    if (x < 3 && y < 2) {
        printf("index=%d\n", result);
    }
    
    return result;
}

// NEW: HAM pixel decoder - processes pixels sequentially to reconstruct RGB values
void decode_ham_pixels(UBYTE **planes, int plane_count, int bytes_per_row, int width, int height, 
                      UBYTE *colormap, int colors, ULONG camg_mode, jpeg_data* data, int orig_width, int orig_height)
{
    printf("Decoding HAM pixels...\n");
    printf("CAMG mode: 0x%08lX\n", camg_mode);
    
    // Determine HAM mode
    BOOL is_ham = (camg_mode & HAM_KEY) != 0;
    BOOL is_ham8 = (plane_count == 8) && is_ham;
    BOOL is_ham6 = (plane_count == 6) && is_ham;
    
    if (!is_ham6 && !is_ham8) {
        printf("ERROR: Not a HAM6 or HAM8 mode!\n");
        return;
    }
    
    printf("Detected %s mode\n", is_ham8 ? "HAM8" : "HAM6");
    
    // HAM6: 6 planes = 2 control + 4 data bits
    // HAM8: 8 planes = 2 control + 6 data bits  
    int data_bits = is_ham8 ? 6 : 4;
    int data_mask = (1 << data_bits) - 1;  // 0x3F for HAM8, 0x0F for HAM6
    int control_shift = data_bits;          // Control bits in upper 2 bits
    
    printf("Data bits: %d, mask: 0x%02X\n", data_bits, data_mask);
    
    // HAM pixel processing requires sequential decoding from left to right
    UBYTE hold_r = 0, hold_g = 0, hold_b = 0;  // Current held RGB values
    
    for (int y = 0; y < height; y++) {
        // Reset hold register at start of each scanline
        hold_r = hold_g = hold_b = 0;
        
        for (int x = 0; x < width; x++) {
            // Get raw pixel value from bitplanes
            UWORD pixel = get_pixel_from_planes_fixed(planes, plane_count, bytes_per_row, x, y, 1 << plane_count);
            
            // Extract control and data bits
            int control = (pixel >> control_shift) & 0x03;  // Upper 2 bits
            int data_val = pixel & data_mask;               // Lower 4 or 6 bits
            
            UBYTE r, g, b;
            
            switch (control) {
                case HAM_SET:
                    // Set pixel from palette
                    if (data_val < colors) {
                        r = hold_r = colormap[data_val * 3 + 0];
                        g = hold_g = colormap[data_val * 3 + 1]; 
                        b = hold_b = colormap[data_val * 3 + 2];
                    } else {
                        r = hold_r = g = hold_g = b = hold_b = 0;
                    }
                    break;
                    
                case HAM_MODIFY_B:
                    // Modify blue component, keep R and G
                    if (is_ham8) {
                        b = hold_b = (data_val << 2) | (data_val >> 4);  // 6-bit to 8-bit
                    } else {
                        b = hold_b = (data_val << 4) | data_val;         // 4-bit to 8-bit
                    }
                    r = hold_r;
                    g = hold_g;
                    break;
                    
                case HAM_MODIFY_R:
                    // Modify red component, keep G and B
                    if (is_ham8) {
                        r = hold_r = (data_val << 2) | (data_val >> 4);  // 6-bit to 8-bit
                    } else {
                        r = hold_r = (data_val << 4) | data_val;         // 4-bit to 8-bit
                    }
                    g = hold_g;
                    b = hold_b;
                    break;
                    
                case HAM_MODIFY_G:
                    // Modify green component, keep R and B  
                    if (is_ham8) {
                        g = hold_g = (data_val << 2) | (data_val >> 4);  // 6-bit to 8-bit
                    } else {
                        g = hold_g = (data_val << 4) | data_val;         // 4-bit to 8-bit
                    }
                    r = hold_r;
                    b = hold_b;
                    break;
            }
            
            // Debug first few pixels
            if (x < 4 && y < 2) {
                printf("HAM Pixel(%d,%d): raw=0x%02X, ctrl=%d, data=0x%02X -> R=%d G=%d B=%d\n", 
                       x, y, pixel, control, data_val, r, g, b);
            }
            
            // Store in output arrays (handling padding)
            if (x < orig_width && y < orig_height) {
                int idx = y * data->width + x;
                data->red[idx] = r;
                data->green[idx] = g;
                data->blue[idx] = b;
            }
        }
    }
    
    printf("HAM decoding completed\n");
}

// Load IFF file directly - ENHANCED WITH HAM SUPPORT
int load_iff_direct(const char *filename, jpeg_data* data)
{
    FILE *file;
    struct IFFHeader header;
    struct IFFChunk chunk;
    struct BMHDChunk bmhd;
    UBYTE *colormap = NULL;
    UBYTE *body_data = NULL;
    UBYTE **planes = NULL;
    ULONG body_size = 0;
    ULONG camg_mode = 0;  // NEW: CAMG viewport mode
    int colors = 0;
    int orig_width, orig_height;
    int padded_width, padded_height;
    int bytes_per_row;
    
    printf("Loading IFF file directly: %s\n", filename);
    
    file = fopen(filename, "rb");
    if (!file) {
        printf("Cannot open file: %s\n", filename);
        return -1;
    }
    
    // Read IFF header
    if (fread(&header, sizeof(header), 1, file) != 1) {
        printf("Cannot read IFF header\n");
        fclose(file);
        return -1;
    }
    
    // Debug: Show raw header data
    printf("Raw header bytes: ");
    unsigned char *raw_header = (unsigned char*)&header;
    for (int i = 0; i < 12; i++) {
        printf("%02X ", raw_header[i]);
    }
    printf("\n");
    
    // Convert from big-endian
    header.id = ntohl(header.id);
    header.size = ntohl(header.size);
    header.type = ntohl(header.type);
    
    // Debug: Show interpreted values
    printf("Header ID: 0x%08X (should be 0x%08X for FORM)\n", header.id, ID_FORM);
    printf("Header size: %lu\n", header.size);
    printf("Header type: 0x%08X (should be 0x%08X for ILBM)\n", header.type, ID_ILBM);
    printf("ID as string: '%.4s'\n", (char*)&header.id);
    printf("Type as string: '%.4s'\n", (char*)&header.type);
    
    if (header.id != ID_FORM || header.type != ID_ILBM) {
        printf("Not a valid ILBM file - got ID=0x%08X, type=0x%08X\n", header.id, header.type);
        fclose(file);
        return -1;
    }
    
    printf("Valid ILBM file detected\n");
    
    // Read chunks
    while (fread(&chunk, sizeof(chunk), 1, file) == 1) {
        chunk.id = ntohl(chunk.id);
        chunk.size = ntohl(chunk.size);
        
        printf("Found chunk: %.4s, size: %lu\n", (char*)&chunk.id, chunk.size);
        
        switch (chunk.id) {
            case ID_BMHD:
                if (fread(&bmhd, sizeof(bmhd), 1, file) != 1) {
                    printf("Cannot read BMHD chunk\n");
                    goto cleanup;
                }
                // Convert from big-endian
                bmhd.w = ntohs(bmhd.w);
                bmhd.h = ntohs(bmhd.h);
                bmhd.x = ntohs(bmhd.x);
                bmhd.y = ntohs(bmhd.y);
                bmhd.transparentColor = ntohs(bmhd.transparentColor);
                bmhd.pageWidth = ntohs(bmhd.pageWidth);
                bmhd.pageHeight = ntohs(bmhd.pageHeight);
                
                printf("Image: %dx%d, %d planes, compression: %d\n", 
                       bmhd.w, bmhd.h, bmhd.nPlanes, bmhd.compression);
                break;
                
            case ID_CMAP:
                colors = chunk.size / 3;
                colormap = AllocVec(chunk.size, MEMF_ANY);
                if (!colormap || fread(colormap, chunk.size, 1, file) != 1) {
                    printf("Cannot read colormap\n");
                    goto cleanup;
                }
                printf("Colormap loaded: %d colors\n", colors);
                // Debug: Print first 16 colormap entries
                for (int i = 0; i < MIN(16, colors); i++) {
                    printf("  Color %d: R=%d G=%d B=%d\n", i, 
                           colormap[i * 3 + 0], colormap[i * 3 + 1], colormap[i * 3 + 2]);
                }
                break;

            case ID_CAMG:  // NEW: Read Amiga viewport mode
                if (chunk.size >= 4) {
                    if (fread(&camg_mode, 4, 1, file) != 1) {
                        printf("Cannot read CAMG chunk\n");
                        goto cleanup;
                    }
                    camg_mode = ntohl(camg_mode);
                    printf("CAMG viewport mode: 0x%08lX", camg_mode);
                    if (camg_mode & HAM_KEY) {
                        printf(" (HAM mode detected!)");
                    }
                    if (camg_mode & EXTRA_HALFBRITE) {
                        printf(" (Extra Halfbrite)");
                    }
                    printf("\n");
                } else {
                    // Skip malformed CAMG chunk
                    fseek(file, chunk.size + (chunk.size & 1), SEEK_CUR);
                }
                break;

            case ID_BODY:
                body_size = chunk.size;
                body_data = AllocVec(body_size, MEMF_ANY);
                if (!body_data || fread(body_data, body_size, 1, file) != 1) {
                    printf("Cannot read BODY chunk\n");
                    goto cleanup;
                }
                printf("BODY chunk loaded: %lu bytes\n", body_size);
                break;
                
            default:
                // Skip unknown chunks (with proper alignment)
                fseek(file, chunk.size + (chunk.size & 1), SEEK_CUR);
                break;
        }
    }
    
    fclose(file);
    file = NULL;
    
    if (!body_data) {
        printf("Missing BODY chunk\n");
        goto cleanup;
    }
    if (!colormap && bmhd.nPlanes <= 8) {
        printf("Missing CMAP chunk for <=8 bitplanes\n");
        goto cleanup;
    }
    
    orig_width = bmhd.w;
    orig_height = bmhd.h;
    
    // Round up to nearest multiple of 16
    padded_width = ((orig_width + 15) / 16) * 16;
    padded_height = ((orig_height + 15) / 16) * 16;
    
    data->width = padded_width;
    data->height = padded_height;
    data->num_pixel = padded_width * padded_height;
    
    printf("Dimensions: %dx%d -> %dx%d (padded)\n", 
           orig_width, orig_height, padded_width, padded_height);
    
    if (alloc_jpeg_data(data) != 0) {
        goto cleanup;
    }
    
    // FIXED: Calculate bytes per row correctly for IFF format
    bytes_per_row = ((orig_width + 15) / 16) * 2;  // Word-aligned
    LONG plane_size = bytes_per_row * orig_height;
    
    printf("Bytes per row: %d, plane size: %ld\n", bytes_per_row, plane_size);
    printf("Image has %d bitplanes, supports %d colors\n", bmhd.nPlanes, 1 << bmhd.nPlanes);
    
    // Allocate plane buffers
    planes = AllocVec(bmhd.nPlanes * sizeof(UBYTE*), MEMF_CLEAR);
    if (!planes) {
        printf("Cannot allocate plane pointers\n");
        goto cleanup;
    }
    
    for (int i = 0; i < bmhd.nPlanes; i++) {
        planes[i] = AllocVec(plane_size, MEMF_ANY);
        if (!planes[i]) {
            printf("Cannot allocate plane %d\n", i);
            goto cleanup;
        }
    }
    
    // FIXED: Improved decompression handling
    printf("BMHD values: w=%d h=%d planes=%d compression=%d\n", 
           bmhd.w, bmhd.h, bmhd.nPlanes, bmhd.compression);
    if (bmhd.compression == 1) {
        printf("Decompressing RLE data (row-interleaved)...\n");
        LONG src_pos = 0;

        for (int row = 0; row < orig_height; row++) {
            for (int plane = 0; plane < bmhd.nPlanes; plane++) {
                LONG dest_offset = row * bytes_per_row;
                UBYTE *dest = planes[plane] + dest_offset;

                LONG bytes_written = 0;
                while (bytes_written < bytes_per_row && src_pos < body_size) {
                    BYTE count = (BYTE)body_data[src_pos++];

                    if (count >= 0) {
                        count++;
                        for (int i = 0; i < count && src_pos < body_size && bytes_written < bytes_per_row; i++) {
                            dest[bytes_written++] = body_data[src_pos++];
                        }
                    } else if (count != -128) {
                        count = -count + 1;
                        if (src_pos < body_size) {
                            UBYTE val = body_data[src_pos++];
                            for (int i = 0; i < count && bytes_written < bytes_per_row; i++) {
                                dest[bytes_written++] = val;
                            }
                        }
                    }
                    // -128 is a no-op
                }

                // Pad any leftover bytes with 0
                while (bytes_written < bytes_per_row) {
                    dest[bytes_written++] = 0;
                }
            }
        }

        printf("RLE decompression complete\n");

    } else {
        // Existing uncompressed handling
        printf("Copying uncompressed BODY data (compression=%d)...\n", bmhd.compression);
        LONG src_pos = 0;
        for (int row = 0; row < orig_height; row++) {
            for (int plane = 0; plane < bmhd.nPlanes; plane++) {
                LONG dest_offset = row * bytes_per_row;
                if (src_pos + bytes_per_row <= body_size) {
                    memcpy(planes[plane] + dest_offset, body_data + src_pos, bytes_per_row);
                    src_pos += bytes_per_row;
                }
            }
        }
    }
  
    // Debug: Check plane data
    printf("Plane data verification:\n");
    for (int plane = 0; plane < MIN(bmhd.nPlanes, 4); plane++) {
        printf("  Plane %d first 16 bytes:", plane);
        for (int i = 0; i < 16 && i < bytes_per_row; i++) {
            printf(" %02X", planes[plane][i]);
        }
        printf("\n");
    }
    
    // NEW: Check if this is a HAM mode image and process accordingly
    BOOL is_ham = (camg_mode & HAM_KEY) != 0;
    BOOL is_ham6 = is_ham && (bmhd.nPlanes == 6);
    BOOL is_ham8 = is_ham && (bmhd.nPlanes == 8);
    
    if (is_ham6 || is_ham8) {
        printf("Processing %s image...\n", is_ham8 ? "HAM8" : "HAM6");
        
        // Use HAM decoder for HAM images
        decode_ham_pixels(planes, bmhd.nPlanes, bytes_per_row, orig_width, orig_height,
                         colormap, colors, camg_mode, data, orig_width, orig_height);
        
        // Fill padding areas with black
        for (int y = 0; y < padded_height; y++) {
            for (int x = 0; x < padded_width; x++) {
                if (x >= orig_width || y >= orig_height) {
                    int idx = y * padded_width + x;
                    data->red[idx] = 0;
                    data->green[idx] = 0;
                    data->blue[idx] = 0;
                }
            }
        }
        
    } else {
        // EXISTING: Convert pixels to RGB with better colormap handling for non-HAM modes
        printf("Converting pixels to RGB (non-HAM mode)...\n");
        int pixel_counts[256] = {0};
        
        for (int y = 0; y < padded_height; y++) {
            for (int x = 0; x < padded_width; x++) {
                int idx = y * padded_width + x;
                UBYTE r, g, b;
                
                if (x < orig_width && y < orig_height) {
                    if (bmhd.nPlanes == 24) {
                        // 24-bit ILBM (8 planes each for R, G, B)
                        UBYTE r_val = 0, g_val = 0, b_val = 0;
                        for (int bit = 0; bit < 8; bit++) {
                            UBYTE r_plane = planes[bit][y * bytes_per_row + (x / 8)];
                            UBYTE g_plane = planes[bit + 8][y * bytes_per_row + (x / 8)];
                            UBYTE b_plane = planes[bit + 16][y * bytes_per_row + (x / 8)];

                            int mask = 1 << (7 - (x % 8));
                            if (r_plane & mask) r_val |= (1 << (7 - bit));
                            if (g_plane & mask) g_val |= (1 << (7 - bit));
                            if (b_plane & mask) b_val |= (1 << (7 - bit));
                        }
                        r = r_val;
                        g = g_val;
                        b = b_val;

                        // Optional debug output
                        if (x < 4 && y < 4) {
                            printf("24-bit Pixel(%d,%d): R=%d G=%d B=%d\n", x, y, r, g, b);
                        }
                    } else {
                        // <=8-bit CLUT image (including EHB)
                        UWORD pixel = get_pixel_from_planes_fixed(planes, bmhd.nPlanes, bytes_per_row, x, y, colors);
                        
                        // Handle Extra Halfbrite (EHB) mode
                        if ((camg_mode & EXTRA_HALFBRITE) && bmhd.nPlanes == 6) {
                            if (pixel >= 32) {
                                // Upper 32 colors are halfbrite versions of lower 32
                                int base_color = pixel - 32;
                                if (base_color < colors) {
                                    r = colormap[base_color * 3 + 0] >> 1;  // Half brightness
                                    g = colormap[base_color * 3 + 1] >> 1;
                                    b = colormap[base_color * 3 + 2] >> 1;
                                } else {
                                    r = g = b = 0;
                                }
                            } else {
                                // Normal palette colors
                                if (pixel < colors) {
                                    r = colormap[pixel * 3 + 0];
                                    g = colormap[pixel * 3 + 1];
                                    b = colormap[pixel * 3 + 2];
                                } else {
                                    r = g = b = 0;
                                }
                            }
                        } else {
                            // Normal palette lookup
                            if (pixel < colors) {
                                r = colormap[pixel * 3 + 0];
                                g = colormap[pixel * 3 + 1];
                                b = colormap[pixel * 3 + 2];
                                pixel_counts[pixel]++;
                            } else {
                                r = g = b = 0;
                            }
                        }
                    }
                } else {
                    r = g = b = 0;  // Padding area
                }

                data->red[idx] = r;
                data->green[idx] = g;
                data->blue[idx] = b;
            }
        }
        
        // Show pixel statistics for non-HAM modes
        printf("Pixel index usage:\n");
        for (int i = 0; i < 16 && i < colors; i++) {
            if (pixel_counts[i] > 0) {
                printf("  Index %d: %d pixels (R=%d G=%d B=%d)\n", 
                       i, pixel_counts[i], 
                       colormap[i * 3 + 0], colormap[i * 3 + 1], colormap[i * 3 + 2]);
            }
        }
    }
    
    // Cleanup
    if (colormap) FreeVec(colormap);
    if (body_data) FreeVec(body_data);
    if (planes) {
        for (int i = 0; i < bmhd.nPlanes; i++) {
            if (planes[i]) FreeVec(planes[i]);
        }
        FreeVec(planes);
    }
    
    printf("IFF file loaded successfully!\n");
    return 0;
    
cleanup:
    if (file) fclose(file);
    if (colormap) FreeVec(colormap);
    if (body_data) FreeVec(body_data);
    if (planes) {
        for (int i = 0; i < bmhd.nPlanes; i++) {
            if (planes[i]) FreeVec(planes[i]);
        }
        FreeVec(planes);
    }
    return -1;
}

// Convert RGB to YCbCr colorspace
int rgb_to_ycbcr(jpeg_data* data)
{
    printf("Converting RGB to YCbCr...\n");
    for (int i = 0; i < data->num_pixel; i++)
    {
        data->y[i]  =       0.299    * data->red[i] + 0.587    * data->green[i] + 0.114    * data->blue[i];
        data->cb[i] = 128 - 0.168736 * data->red[i] - 0.331264 * data->green[i] + 0.5      * data->blue[i];
        data->cr[i] = 128 + 0.5      * data->red[i] - 0.418688 * data->green[i] - 0.081312 * data->blue[i];
        
        // Clamp values
        data->y[i] = CLIP(data->y[i], 0, 255);
        data->cb[i] = CLIP(data->cb[i], 0, 255);
        data->cr[i] = CLIP(data->cr[i], 0, 255);
    }
    return 0;
}

// Subsample chroma channels (4:2:0)
void subsample_chroma(jpeg_data* data)
{
    printf("Subsampling chroma channels...\n");
    for (int h = 0; h < data->height/2; h++)
        for (int w = 0; w < data->width/2; w++)
        {
            int i = 2*h*data->width + 2*w;
            data->cb_sub[h*data->width/2+w] = (data->cb[i] + data->cb[i+1] + data->cb[i+data->width] + data->cb[i+data->width+1]) / 4;
            data->cr_sub[h*data->width/2+w] = (data->cr[i] + data->cr[i+1] + data->cr[i+data->width] + data->cr[i+data->width+1]) / 4;
        }
}

// Initialize DCT lookup table
void init_dct_lookup(void)
{
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            cos_lookup[i][j] = cos((2*i+1)*j*M_PI/16);
}

// DCT transform for 8x8 block
static void dct_block(int gap, int in[], double out[])
{
    double inner_lookup[8][8];
    
    for (int x_t = 0; x_t < 8; x_t++)
        for (int y_f = 0; y_f < 8; y_f++)
        {
            inner_lookup[x_t][y_f] = 0;
            for (int y_t = 0; y_t < 8; y_t++)
                inner_lookup[x_t][y_f] += (in[y_t*gap+x_t] - 128) * cos_lookup[y_t][y_f];
        }

    for (int y_f = 0; y_f < 8; y_f++)
        for (int x_f = 0; x_f < 8; x_f++)
        {
            double freq = 0;
            for (int x_t = 0; x_t < 8; x_t++)
                freq += inner_lookup[x_t][y_f] * cos_lookup[x_t][x_f];

            if (x_f == 0) freq *= M_SQRT1_2;
            if (y_f == 0) freq *= M_SQRT1_2;
            freq /= 4;

            out[y_f*8+x_f] = freq;
        }
}

// Perform DCT on all blocks
void dct(int blocks_horiz, int blocks_vert, int in[], double out[])
{
    printf("Performing DCT transform...\n");
    for (int v = 0; v < blocks_vert; v++)
        for (int h = 0; h < blocks_horiz; h++)
            dct_block(8*blocks_horiz, in + v*blocks_horiz*64 + h*8, out + (v*blocks_horiz+h)*64);
}

// Set quantization quality
void set_quality(int quantizer[], int quality)
{
    for (int i = 0; i < 64; i++)
        quantizer[i] = CLIP((100-quality)/50.0 * quantizer[i], 1, 255);
}

// Quantize DCT coefficients
void quantize(int num_pixel, double in[], int out[], int quantizer[])
{
    printf("Quantizing coefficients...\n");
    for (int i = 0; i < num_pixel; i++)
    {
        out[i] = in[i] / quantizer[i%64];
        out[i] = CLIP(out[i], -2048, 2047);
    }
}

// Reorder coefficients in zig-zag pattern
void zigzag(int num_pixel, int dct[])
{
    printf("Reordering coefficients (zig-zag)...\n");
    int tmp[64];
    for (int i = 0; i < num_pixel; i += 64)
    {
        for (int j = 0; j < 64; j++)
            tmp[j] = dct[i+j];
        for (int j = 0; j < 64; j++)
            dct[i+j] = tmp[scan_order[j]];
    }
}

// Convert DC coefficients to differential values
void diff_dc(int num_pixel, int dct_quant[])
{
    printf("Converting DC to differential values...\n");
    int last = dct_quant[0];
    for (int i = 64; i < num_pixel; i += 64)
    {
        dct_quant[i] -= last;
        last += dct_quant[i];
    }
}

// Standard JPEG Huffman tables (from JPEG specification)
// Luma DC code lengths
UBYTE std_luma_dc_lens[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
UBYTE std_luma_dc_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

// Luma AC code lengths  
UBYTE std_luma_ac_lens[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
UBYTE std_luma_ac_vals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

// Chroma DC code lengths
UBYTE std_chroma_dc_lens[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
UBYTE std_chroma_dc_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

// Chroma AC code lengths
UBYTE std_chroma_ac_lens[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
UBYTE std_chroma_ac_vals[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

// Build Huffman codes from standard tables
void build_standard_huffman_table(huff_code* hc, UBYTE* lens, UBYTE* vals, int num_vals)
{
    printf("    Building from standard table...\n");
    
    // Clear arrays
    for (int i = 0; i < 256; i++) {
        hc->sym_code_len[i] = 0;
        hc->sym_code[i] = 0;
    }
    
    // Copy length frequencies
    for (int i = 0; i < 16; i++) {
        hc->code_len_freq[i+1] = lens[i];
    }
    
    // Assign code lengths to symbols
    int k = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < lens[i-1]; j++) {
            if (k < num_vals) {
                hc->sym_code_len[vals[k]] = i;
                hc->sym_sorted[k] = vals[k];
                k++;
            }
        }
    }
    
    // Generate codes
    int code = 0;
    int si = 1;
    for (int i = 0; i < k; i++) {
        while (hc->sym_code_len[hc->sym_sorted[i]] > si) {
            code <<= 1;
            si++;
        }
        hc->sym_code[hc->sym_sorted[i]] = code;
        code++;
    }
    
    printf("    Standard table built with %d symbols\n", k);
}

// Fast initialization using standard JPEG Huffman tables
void init_huffman(jpeg_data* data)
{
    printf("Initializing standard JPEG Huffman tables...\n");
    
    printf("Building luma DC table...\n");
    build_standard_huffman_table(&data->luma_dc, std_luma_dc_lens, std_luma_dc_vals, 12);
    
    printf("Building luma AC table...\n");
    build_standard_huffman_table(&data->luma_ac, std_luma_ac_lens, std_luma_ac_vals, 162);
    
    printf("Building chroma DC table...\n");
    build_standard_huffman_table(&data->chroma_dc, std_chroma_dc_lens, std_chroma_dc_vals, 12);
    
    printf("Building chroma AC table...\n");
    build_standard_huffman_table(&data->chroma_ac, std_chroma_ac_lens, std_chroma_ac_vals, 162);
    
    printf("All standard Huffman tables completed!\n");
}

// Huffman class calculation
int huff_class(int value)
{
    value = value < 0 ? -value : value;
    int class = 0;
    while (value > 0)
    {
        value = value >> 1;
        class++;
    }
    return class;
}

// Bit writing functions
void write_byte(FILE* f, int code_word, int start, int end)
{
    if (start == end) return;

    if (end > 0)
    {
        code_word <<= end;
        code_word &= (1<<start)-1;
        byte_buffer |= code_word;
        bits_written += start-end;
    }
    else
    {
        int part2 = code_word & ((1<<(-end))-1);
        code_word >>= (-end);
        code_word &= (1<<start)-1;
        byte_buffer |= code_word;
        fputc(byte_buffer, f);
        if (byte_buffer == 0xFF)
            fputc(0, f); // byte stuffing
        bits_written = 0;
        byte_buffer = 0;
        write_byte(f, part2, 8, 8+end);
        return;
    }
}

void write_bits(FILE* f, int code_word, int code_len)
{
    if (code_len == 0) return;
    write_byte(f, code_word, 8-bits_written, 8-bits_written-code_len);
}

void fill_last_byte(FILE* f)
{
    byte_buffer |= (1<<(8-bits_written))-1;
    fputc(byte_buffer, f);
    byte_buffer = 0;
    bits_written = 0;
}

// Encode DC coefficient
void encode_dc_value(FILE* f, int dc_val, huff_code* huff)
{
    int class = huff_class(dc_val);
    int class_code = huff->sym_code[class];
    int class_size = huff->sym_code_len[class];
    write_bits(f, class_code, class_size);

    unsigned int id = abs(dc_val);
    if (dc_val < 0) id = ~id;
    write_bits(f, id, class);
}

// Encode AC coefficient
void encode_ac_value(FILE* f, int ac_val, int num_zeros, huff_code* huff)
{
    int class = huff_class(ac_val);
    int v = ((num_zeros<<4)&0xF0) | (class&0x0F);
    int code = huff->sym_code[v];
    int size = huff->sym_code_len[v];
    write_bits(f, code, size);

    unsigned int id = abs(ac_val);
    if (ac_val < 0) id = ~id;
    write_bits(f, id, class);
}

// Write coefficients for one component
void write_coefficients(FILE* f, int num_pixel, int dct_quant[], huff_code* huff_dc, huff_code* huff_ac)
{
    int num_zeros = 0;
    int last_nonzero;
    
    for (int i = 0; i < num_pixel; i++)
    {
        if (i%64 == 0)
        {
            encode_dc_value(f, dct_quant[i], huff_dc);
            for (last_nonzero = i+63; last_nonzero > i; last_nonzero--)
                if (dct_quant[last_nonzero] != 0)
                    break;
            continue;
        }
    
        if (i == last_nonzero + 1)
        {
            write_bits(f, huff_ac->sym_code[0x00], huff_ac->sym_code_len[0x00]); // EOB
            i = (i/64+1)*64-1;
            continue;
        }

        if (dct_quant[i] == 0)
        {
            num_zeros++;
            if (num_zeros == 16)
            {
                write_bits(f, huff_ac->sym_code[0xF0], huff_ac->sym_code_len[0xF0]); // ZRL
                num_zeros = 0;
            }
            continue;
        }

        encode_ac_value(f, dct_quant[i], num_zeros, huff_ac);
        num_zeros = 0;
    }
}

// Write DHT header
void write_dht_header(FILE* f, int code_len_freq[], int sym_sorted[], int tc_th)
{
    int length = 19;
    for (int i = 1; i <= 16; i++)
        length += code_len_freq[i];

    fputc(0xFF, f); fputc(0xC4, f); // DHT marker

    fputc((length>>8)&0xFF, f); fputc(length&0xFF, f); // length

    fputc(tc_th, f); // table class and ID

    for (int i = 1; i <= 16; i++)
        fputc(code_len_freq[i], f); // code lengths

    for (int i = 0; length > 19; i++, length--)
        fputc(sym_sorted[i], f); // symbols
}

// Write complete JPEG file
void write_file(const char* file_name, jpeg_data* data)
{
    printf("Writing JPEG file: %s\n", file_name);
    
    FILE* f = fopen(file_name, "wb");
    if (!f) {
        printf("Cannot create output file: %s\n", file_name);
        return;
    }

    // Reset bit writer
    byte_buffer = 0;
    bits_written = 0;

    // SOI marker
    fputc(0xFF, f); fputc(0xD8, f);

    // APP0 marker (JFIF)
    fputc(0xFF, f);	fputc(0xE0, f);
        fputc(0, f); fputc(16, f); // length
        fputc(0x4A, f); fputc(0x46, f); fputc(0x49, f); fputc(0x46, f); fputc(0x00, f); // "JFIF"
        fputc(0x01, f); fputc(0x01, f); // version 1.1
        fputc(0x00, f); // units (no units)
        fputc(0x00, f); fputc(0x48, f); // X density (72)
        fputc(0x00, f); fputc(0x48, f); // Y density (72)
        fputc(0x00, f); // thumbnail width
        fputc(0x00, f); // thumbnail height

    //DT marker (luma quantization table)
    fputc(0xFF, f); fputc(0xDB, f);
        fputc(0, f); fputc(67, f); // length
        fputc(0, f); // table ID
        for (int i = 0; i < 64; i++)
            fputc(luma_quantizer[scan_order[i]], f);

    // DQT marker (chroma quantization table)
    fputc(0xFF, f); fputc(0xDB, f);
        fputc(0, f); fputc(67, f); // length
        fputc(1, f); // table ID
        for (int i = 0; i < 64; i++)
            fputc(chroma_quantizer[scan_order[i]], f);

    // DHT markers (Huffman tables)
    write_dht_header(f, data->luma_dc.code_len_freq, data->luma_dc.sym_sorted, 0x00);
    write_dht_header(f, data->luma_ac.code_len_freq, data->luma_ac.sym_sorted, 0x10);
    write_dht_header(f, data->chroma_dc.code_len_freq, data->chroma_dc.sym_sorted, 0x01);
    write_dht_header(f, data->chroma_ac.code_len_freq, data->chroma_ac.sym_sorted, 0x11);

    // SOF0 marker (Start of Frame - Baseline DCT)
    fputc(0xFF, f); fputc(0xC0, f);
        fputc(0, f); fputc(17, f); // length
        fputc(0x08, f); // precision (8 bits)
        fputc((data->height>>8)&0xFF, f); fputc(data->height&0xFF, f); // height
        fputc((data->width>>8)&0xFF, f); fputc(data->width&0xFF, f); // width
        fputc(0x03, f); // number of components
        fputc(1, f); // Y component ID
        fputc(0x22, f); // Y sampling factor (2x2)
        fputc(0, f); // Y quantization table
        fputc(2, f); // Cb component ID
        fputc(0x11, f); // Cb sampling factor (1x1)
        fputc(1, f); // Cb quantization table
        fputc(3, f); // Cr component ID
        fputc(0x11, f); // Cr sampling factor (1x1)
        fputc(1, f); // Cr quantization table

    // SOS marker (Start of Scan - Y component)
    fputc(0xFF, f); fputc(0xDA, f);
        fputc(0, f); fputc(8, f); // length
        fputc(1, f); // number of components
        fputc(1, f); // Y component ID
        fputc(0x00, f); // Huffman table selection
        fputc(0x00, f); // start of spectral selection
        fputc(0x3F, f); // end of spectral selection
        fputc(0x00, f); // successive approximation
        write_coefficients(f, data->num_pixel, data->dct_y_quant, &data->luma_dc, &data->luma_ac);
        fill_last_byte(f);

    // SOS marker (Start of Scan - Cb component)
    fputc(0xFF, f); fputc(0xDA, f);
        fputc(0, f); fputc(8, f); // length
        fputc(1, f); // number of components
        fputc(2, f); // Cb component ID
        fputc(0x11, f); // Huffman table selection
        fputc(0x00, f); fputc(0x3F, f); fputc(0x00, f);
        write_coefficients(f, data->num_pixel/4, data->dct_cb_quant, &data->chroma_dc, &data->chroma_ac);
        fill_last_byte(f);

    // SOS marker (Start of Scan - Cr component)
    fputc(0xFF, f); fputc(0xDA, f);
        fputc(0, f); fputc(8, f); // length
        fputc(1, f); // number of components
        fputc(3, f); // Cr component ID
        fputc(0x11, f); // Huffman table selection
        fputc(0x00, f); fputc(0x3F, f); fputc(0x00, f);
        write_coefficients(f, data->num_pixel/4, data->dct_cr_quant, &data->chroma_dc, &data->chroma_ac);
        fill_last_byte(f);

    // EOI marker
    fputc(0xFF, f); fputc(0xD9, f);

    fclose(f);
    printf("JPEG file written successfully\n");
}

// Free allocated memory
void free_jpeg_data(jpeg_data* data)
{
    if (data->red) FreeVec(data->red);
    if (data->green) FreeVec(data->green);
    if (data->blue) FreeVec(data->blue);
    if (data->y) FreeVec(data->y);
    if (data->cb) FreeVec(data->cb);
    if (data->cr) FreeVec(data->cr);
    if (data->cb_sub) FreeVec(data->cb_sub);
    if (data->cr_sub) FreeVec(data->cr_sub);
    if (data->dct_y) FreeVec(data->dct_y);
    if (data->dct_cb) FreeVec(data->dct_cb);
    if (data->dct_cr) FreeVec(data->dct_cr);
    if (data->dct_y_quant) FreeVec(data->dct_y_quant);
    if (data->dct_cb_quant) FreeVec(data->dct_cb_quant);
    if (data->dct_cr_quant) FreeVec(data->dct_cr_quant);
}

// Parse command line arguments
BOOL parse_arguments(LONG *args, struct Options *opts)
{
    opts->input = (STRPTR)args[0];
    opts->output = (STRPTR)args[1];
    opts->quality = (LONG *)args[2];
    return TRUE;
}

// Main function
int main(void)
{
    LONG args[3] = {0};
    struct Options opts = {0};
    struct RDArgs *rdargs = NULL;
    int quality = 85; // default quality

    printf("Amiga JPEG Encoder starting (Direct IFF with HAM6/HAM8 support)...\n");

    rdargs = ReadArgs(TEMPLATE, args, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Error parsing arguments");
        printf("Usage: jpeg-encode <input.iff> <output.jpg> [QUALITY <1-100>]\n");
        printf("Example: jpeg-encode RAM:Circles.iff RAM:Circles.jpg QUALITY 85\n");
        printf("Supports: 8-bit palette, 24-bit RGB, HAM6, HAM8, and EHB modes\n");
        return RETURN_ERROR;
    }

    if (!parse_arguments(args, &opts)) {
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }

    if (!opts.input || !opts.output) {
        printf("ERROR: Both input and output files must be specified\n");
        printf("Usage: jpeg-encode <input.iff> <output.jpg> [QUALITY <1-100>]\n");
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }

    if (opts.quality) {
        quality = *opts.quality;
        if (quality < 1 || quality > 100) {
            printf("Quality must be between 1 and 100\n");
            FreeArgs(rdargs);
            return RETURN_ERROR;
        }
    }

    printf("Input file: %s\n", opts.input);
    printf("Output file: %s\n", opts.output);
    printf("Quality: %d\n", quality);
    
    // Check if input file exists
    BPTR lock = Lock(opts.input, ACCESS_READ);
    if (!lock) {
        printf("ERROR: Cannot access input file: %s\n", opts.input);
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }
    UnLock(lock);
    printf("Input file found and accessible\n");

    // Set quantization quality
    printf("Setting quantization quality...\n");
    set_quality(luma_quantizer, quality);
    set_quality(chroma_quantizer, quality);

    jpeg_data data;
    memset(&data, 0, sizeof(data));

    printf("Loading IFF file directly...\n");
    // Load IFF file using direct method only
    if (load_iff_direct((const char *)opts.input, &data) != 0) {
        printf("Failed to load IFF file\n");
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }
    printf("IFF file loaded successfully\n");

    printf("About to convert to YCbCr...\n");
    // Convert to YCbCr
    if (rgb_to_ycbcr(&data) != 0) {
        printf("Failed to convert to YCbCr\n");
        free_jpeg_data(&data);
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }
    printf("YCbCr conversion completed\n");

    printf("About to subsample chroma...\n");
    // Subsample chroma
    subsample_chroma(&data);
    printf("Chroma subsampling completed\n");

    printf("Initializing DCT lookup table...\n");
    // Initialize DCT
    init_dct_lookup();
    printf("DCT lookup table initialized\n");

    printf("Performing DCT transforms...\n");
    // Perform DCT
    dct(data.width/8, data.height/8, data.y, data.dct_y);
    printf("Y component DCT completed\n");
    dct(data.width/16, data.height/16, data.cb_sub, data.dct_cb);
    printf("Cb component DCT completed\n");
    dct(data.width/16, data.height/16, data.cr_sub, data.dct_cr);
    printf("Cr component DCT completed\n");

    printf("Quantizing coefficients...\n");
    // Quantize
    quantize(data.num_pixel, data.dct_y, data.dct_y_quant, luma_quantizer);
    quantize(data.num_pixel/4, data.dct_cb, data.dct_cb_quant, chroma_quantizer);
    quantize(data.num_pixel/4, data.dct_cr, data.dct_cr_quant, chroma_quantizer);
    printf("Quantization completed\n");

    printf("Reordering coefficients...\n");
    // Zig-zag reorder
    zigzag(data.num_pixel, data.dct_y_quant);
    zigzag(data.num_pixel/4, data.dct_cb_quant);
    zigzag(data.num_pixel/4, data.dct_cr_quant);
    printf("Zig-zag reordering completed\n");

    printf("Converting DC coefficients...\n");
    // Convert DC to differential
    diff_dc(data.num_pixel, data.dct_y_quant);
    diff_dc(data.num_pixel/4, data.dct_cb_quant);
    diff_dc(data.num_pixel/4, data.dct_cr_quant);
    printf("DC conversion completed\n");

    printf("Initializing Huffman tables...\n");
    // Initialize Huffman tables
    init_huffman(&data);
    printf("Huffman tables initialized\n");

    printf("Writing JPEG file...\n");
    // Write JPEG file
    write_file((const char *)opts.output, &data);
    printf("JPEG file written\n");

    printf("Cleaning up memory...\n");
    // Cleanup
    free_jpeg_data(&data);
    FreeArgs(rdargs);

    printf("Conversion completed successfully!\n");
    return RETURN_OK;
}