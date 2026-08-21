#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../driver/media_size.h"
#include "../driver/pwg_writer.h"

struct TestSink {
    unsigned char header[1800];
    unsigned long bytes;
};

static long sink_write(void *ctx, const unsigned char *data,
                       unsigned long length)
{
    struct TestSink *sink = (struct TestSink *)ctx;
    unsigned long copy = length;

    if (sink->bytes >= sizeof(sink->header)) copy = 0;
    else if (copy > sizeof(sink->header) - sink->bytes)
        copy = sizeof(sink->header) - sink->bytes;
    if (copy) memcpy(sink->header + sink->bytes, data, copy);
    sink->bytes += length;
    return (long)length;
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) |
           ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) |
           (unsigned long)p[3];
}

int main(void)
{
    unsigned long x = 0, y = 0;
    unsigned long width = 2478UL, height, row;
    unsigned long scratch_size;
    unsigned char *scratch, *rgb;
    struct TestSink sink;
    MPPwgEncoder encoder;

    assert(mp_media_dimensions_100mm("iso_a4_210x297mm", &x, &y));
    assert(x == 21000UL && y == 29700UL);
    assert(mp_media_dimensions_100mm("na_letter_8.5x11in", &x, &y));
    assert(x == 21590UL && y == 27940UL);
    assert(!mp_media_dimensions_100mm("unknown", &x, &y));

    /* Exact dimensions observed in the Wordworth regression log. */
    assert(mp_media_target_height("iso_a4_210x297mm", 2478UL, 300UL) ==
           3505UL);
    assert(mp_media_target_height("iso_a4_210x297mm", 3508UL, 300UL) ==
           2480UL);
    assert(mp_media_target_height("na_letter_8.5x11in", 2550UL, 300UL) ==
           3300UL);
    assert(mp_media_target_height("unknown", 2478UL, 300UL) == 0UL);

    assert(mp_is_tiny_auxiliary_band(2478UL, 4UL));
    assert(!mp_is_tiny_auxiliary_band(2478UL, 100UL));
    assert(!mp_is_tiny_auxiliary_band(200UL, 4UL));

    /* The fixed Wordworth path starts PWG at the complete media height,
     * then writes/pads exactly that many rows. Its header must therefore
     * remain portrait-shaped and internally consistent. */
    height = mp_media_target_height("iso_a4_210x297mm", width, 300UL);
    scratch_size = mp_pwg_scratch_size(width);
    scratch = (unsigned char *)malloc(scratch_size);
    rgb = (unsigned char *)malloc(width * 3UL);
    assert(scratch && rgb);
    memset(&sink, 0, sizeof(sink));
    memset(rgb, 255, width * 3UL);
    assert(mp_pwg_begin(&encoder, width, height, 0, 0, 300UL,
                        scratch, scratch_size, sink_write, &sink));
    assert(be32(sink.header + MP_PWG_PAGESIZE_Y_FIELD_OFFSET) == 841UL);
    assert(be32(sink.header + MP_PWG_HEIGHT_FIELD_OFFSET) == height);
    assert(be32(sink.header + MP_PWG_ROWCOUNT_FIELD_OFFSET) == height);
    for (row = 0; row < height; ++row)
        assert(mp_pwg_write_scanline(&encoder, rgb));
    assert(mp_pwg_finish(&encoder));
    free(rgb);
    free(scratch);

    puts("media/page geometry tests passed");
    return 0;
}
