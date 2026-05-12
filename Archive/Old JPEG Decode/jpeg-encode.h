#ifndef JPEG_ENCODE_H
#define JPEG_ENCODE_H

#include <exec/types.h> // for ULONG, etc.

typedef struct __jpeg_encoder_data {
    int width;
    int height;
    int num_pixel;
    unsigned char *red;
    unsigned char *green;
    unsigned char *blue;
    // Add any other members if needed
} jpeg_encoder_data;

int alloc_jpeg_encoder_data(jpeg_encoder_data* data);
void set_quality_jpeg(int quantizer[], int quality);
void rgb_to_ycbcr_jpeg(unsigned char *rgb_data, jpeg_encoder_data *data);
void subsample_chroma_jpeg(jpeg_encoder_data* data);
void init_huffman_jpeg(jpeg_encoder_data* data);
void free_jpeg_encoder_data(jpeg_encoder_data* data);
int send_jpeg_print_job(const char* ip, int port, const char* media, const char* print_mode, unsigned char* jpeg_data, int jpeg_size);

#endif
