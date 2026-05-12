#ifndef MY_JPEG_PROTO_H
#define MY_JPEG_PROTO_H

#include <exec/types.h>
#include <libraries/jpeg.h>

/* Function prototypes without the register specifiers */
ULONG AllocJPEGDecompressA(struct JPEGDecHandle **, struct TagItem *);
ULONG AllocJPEGDecompress(struct JPEGDecHandle **, Tag, ...);
ULONG FreeJPEGDecompress(struct JPEGDecHandle *);
ULONG DecompressJPEGA(struct JPEGDecHandle *, struct TagItem *);
ULONG DecompressJPEG(struct JPEGDecHandle *, Tag, ...);

ULONG AllocJPEGCompressA(struct JPEGComHandle **, struct TagItem *);
ULONG AllocJPEGCompress(struct JPEGComHandle **, Tag, ...);
ULONG FreeJPEGCompress(struct JPEGComHandle *);
ULONG CompressJPEGA(struct JPEGComHandle *, struct TagItem *);
ULONG CompressJPEG(struct JPEGComHandle *, Tag, ...);

void *AllocBufferFromJPEGA(struct JPEGDecHandle *, struct TagItem *);
void *AllocBufferFromJPEG(struct JPEGDecHandle *, Tag, ...);
void FreeJPEGBuffer(void *);
ULONG GetJPEGInfoA(void *, struct TagItem *);
ULONG GetJPEGInfo(void *, Tag, ...);
struct JPEGMarker *GetJPEGMarkerA(void *, struct TagItem *);
struct JPEGMarker *GetJPEGMarker(void *, Tag, ...);

/* Library base externals */
extern struct Library *JPEGBase;

#endif /* MY_JPEG_PROTO_H */