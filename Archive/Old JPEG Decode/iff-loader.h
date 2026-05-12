#ifndef IFF_LOADER_H
#define IFF_LOADER_H

struct jpeg_data {
    int width;
    int height;
    int num_pixel;
    unsigned char *red;
    unsigned char *green;
    unsigned char *blue;
};

int load_iff_direct(const char *filename, struct jpeg_data *data);
void free_jpeg_data(struct jpeg_data *data);

#endif
