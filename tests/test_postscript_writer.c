#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postscript_writer.h"

struct Sink {
    FILE *file;
};

static long sink_write(void *ctx, const unsigned char *data, unsigned long len)
{
    struct Sink *sink = (struct Sink *)ctx;
    return fwrite(data, 1, len, sink->file) == len ? (long)len : -1;
}

static int file_contains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *data;
    int found;

    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = (char *)malloc((size_t)size + 1U);
    if (!data) { fclose(file); return 0; }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 0;
    }
    data[size] = '\0';
    found = strstr(data, needle) != NULL;
    free(data);
    fclose(file);
    return found;
}

int main(int argc, char **argv)
{
    const unsigned long width = 32;
    const unsigned long height = 24;
    const char *path = argc > 1 ? argv[1] : "test-postscript.ps";
    unsigned long scratch_size = mp_postscript_scratch_size(width);
    unsigned char *scratch = (unsigned char *)malloc(scratch_size);
    unsigned char row[32 * 3];
    MPPostScriptEncoder encoder;
    struct Sink sink;
    unsigned long x, y;

    if (!scratch) return 2;
    sink.file = fopen(path, "wb");
    if (!sink.file) { free(scratch); return 3; }

    if (!mp_postscript_begin(&encoder, width, height, 300,
                             scratch, scratch_size, sink_write, &sink))
        return 4;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            row[x * 3 + 0] = (unsigned char)(x * 7U);
            row[x * 3 + 1] = (unsigned char)(y * 10U);
            row[x * 3 + 2] = (unsigned char)((x + y) * 4U);
        }
        if (!mp_postscript_write_scanline(&encoder, row)) return 5;
    }
    if (!mp_postscript_finish(&encoder)) return 6;
    if (fclose(sink.file) != 0) return 7;
    free(scratch);

    if (!file_contains(path, "%!PS-Adobe-3.0\n")) return 8;
    if (!file_contains(path, "/ASCII85Decode filter /DCTDecode filter")) return 9;
    if (!file_contains(path, "\n~>\ngrestore\nshowpage\n")) return 10;
    if (!file_contains(path, "\n%%EOF\n")) return 11;

    return 0;
}
