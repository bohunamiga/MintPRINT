#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <inline/macros.h>
#include <jpeg/jpeg.h>

struct Library *JpegBase = NULL;

ULONG AllocJPEGDecompressA(struct JPEGDecHandle **jph, struct TagItem *tags) {
    return LP2(0x1e, ULONG, AllocJPEGDecompressA,
               struct JPEGDecHandle **, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

ULONG FreeJPEGDecompress(struct JPEGDecHandle *jph) {
    return LP1(0x24, ULONG, FreeJPEGDecompress,
               struct JPEGDecHandle *, jph, A0,
               struct Library *, JpegBase);
}

ULONG DecompressJPEGA(struct JPEGDecHandle *jph, struct TagItem *tags) {
    return LP2(0x2a, ULONG, DecompressJPEGA,
               struct JPEGDecHandle *, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

ULONG AllocJPEGCompressA(struct JPEGComHandle **jph, struct TagItem *tags) {
    return LP2(0x30, ULONG, AllocJPEGCompressA,
               struct JPEGComHandle **, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

ULONG FreeJPEGCompress(struct JPEGComHandle *jph) {
    return LP1(0x36, ULONG, FreeJPEGCompress,
               struct JPEGComHandle *, jph, A0,
               struct Library *, JpegBase);
}

ULONG CompressJPEGA(struct JPEGComHandle *jph, struct TagItem *tags) {
    return LP2(0x3c, ULONG, CompressJPEGA,
               struct JPEGComHandle *, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

void *AllocBufferFromJPEGA(struct JPEGDecHandle *jph, struct TagItem *tags) {
    return LP2(0x42, void *, AllocBufferFromJPEGA,
               struct JPEGDecHandle *, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

void FreeJPEGBuffer(void *buffer) {
    LP1NR(0x48, FreeJPEGBuffer,
          void *, buffer, A0,
          struct Library *, JpegBase);
}

ULONG GetJPEGInfoA(void *jph, struct TagItem *tags) {
    return LP2(0x4e, ULONG, GetJPEGInfoA,
               void *, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}

struct JPEGMarker *GetJPEGMarkerA(void *jph, struct TagItem *tags) {
    return LP2(0x54, struct JPEGMarker *, GetJPEGMarkerA,
               void *, jph, A0,
               struct TagItem *, tags, A1,
               struct Library *, JpegBase);
}
