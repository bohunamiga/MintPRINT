#ifndef JPEG_STUB_H
#define JPEG_STUB_H

#include <exec/types.h>
#include <exec/libraries.h>

struct JPEGInfo {
    ULONG Width;
    ULONG Height;
    ULONG Quality;
    void *Image;
    UBYTE DataType;
};

#define JPEGDT_RGB 2

extern struct Library *jpegbase;
ULONG WriteJpeg(BPTR file, struct JPEGInfo *info);

#endif
