/*
 * PWG Raster page header layout, sync word, enum values, and the row
 * compression scheme are per the PWG 5102.4 "PWG Raster Format" spec
 * (CUPS raster v2 compatible): sync word "RaS2", a fixed 1796-byte page
 * header, then row data compressed with a PackBits-style scheme, all
 * multi-byte header fields in network byte order. Verified against the
 * public CUPS reference implementation (cups/raster.h, cups/raster-stream.c)
 * rather than from memory alone, since a byte-layout mistake here would
 * only surface as garbled physical output with no useful error message.
 */

#include "pwg_writer.h"

static int mp_pwg_raw(MPPwgEncoder *e, const unsigned char *p, unsigned long n)
{
    if (e->failed) return 0;
    if (!n) return 1;
    if (!e->write_fn || e->write_fn(e->write_ctx, p, n) != (long)n) {
        e->failed = 1;
        return 0;
    }
    return 1;
}

static int mp_pwg_zeros(MPPwgEncoder *e, unsigned long n)
{
    static const unsigned char zero64[64] = { 0 };
    while (n > 0) {
        unsigned long chunk = n > 64UL ? 64UL : n;
        if (!mp_pwg_raw(e, zero64, chunk)) return 0;
        n -= chunk;
    }
    return 1;
}

static int mp_pwg_u32(MPPwgEncoder *e, unsigned long v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
    return mp_pwg_raw(e, b, 4);
}

/* Writes the sync word plus the 1796-byte page header for one srgb_8
 * (8-bit sRGB, chunked/interleaved RGB) page. Field order/sizes match
 * cups_page_header2_t exactly - see the file comment. Anything not
 * relevant to a single simple page (media handling, ICC/rendering-intent
 * strings, etc.) is left zeroed, which is its documented default. */
static int mp_pwg_write_header(MPPwgEncoder *e)
{
    static const unsigned char sync[4] = { 'R', 'a', 'S', '2' };

    if (!mp_pwg_raw(e, sync, 4)) return 0;

    /* MediaClass, MediaColor, MediaType, OutputType: char[64] x 4 */
    if (!mp_pwg_zeros(e, 64UL * 4UL)) return 0;

    /* AdvanceDistance, AdvanceMedia, Collate, CutMedia, Duplex */
    if (!mp_pwg_zeros(e, 4UL * 5UL)) return 0;

    /* HWResolution[2] */
    if (!mp_pwg_u32(e, e->dpi) || !mp_pwg_u32(e, e->dpi)) return 0;

    /* ImagingBoundingBox[4] */
    if (!mp_pwg_zeros(e, 4UL * 4UL)) return 0;

    /* InsertSheet, Jog, LeadingEdge */
    if (!mp_pwg_zeros(e, 4UL * 3UL)) return 0;

    /* Margins[2] */
    if (!mp_pwg_zeros(e, 4UL * 2UL)) return 0;

    /* ManualFeed, MediaPosition, MediaWeight, MirrorPrint, NegativePrint */
    if (!mp_pwg_zeros(e, 4UL * 5UL)) return 0;

    /* NumCopies */
    if (!mp_pwg_u32(e, 1UL)) return 0;

    /* Orientation, OutputFaceUp */
    if (!mp_pwg_zeros(e, 4UL * 2UL)) return 0;

    /* PageSize[2] - points (1/72in). Deliberately the caller-supplied
     * physical page size (see mp_pwg_begin), NOT derived from the pixel
     * raster - a page size computed purely from width/height at an
     * assumed DPI can (and, on real hardware, did) end up contradicting
     * whatever media the surrounding IPP job attributes already told the
     * printer to use, which at least one real printer flatly rejected as
     * a document-format error rather than reconciling the two. */
    if (!mp_pwg_u32(e, e->page_pts_x) ||
        !mp_pwg_u32(e, e->page_pts_y)) return 0;

    /* Separations, TraySwitch, Tumble */
    if (!mp_pwg_zeros(e, 4UL * 3UL)) return 0;

    /* cupsWidth, cupsHeight */
    if (!mp_pwg_u32(e, e->width) || !mp_pwg_u32(e, e->height)) return 0;

    /* cupsMediaType */
    if (!mp_pwg_zeros(e, 4UL)) return 0;

    /* cupsBitsPerColor, cupsBitsPerPixel, cupsBytesPerLine */
    if (!mp_pwg_u32(e, 8UL) || !mp_pwg_u32(e, 24UL) ||
        !mp_pwg_u32(e, e->bytes_per_line)) return 0;

    /* cupsColorOrder (0 = chunked/interleaved), cupsColorSpace (19 = sRGB) */
    if (!mp_pwg_u32(e, 0UL) || !mp_pwg_u32(e, 19UL)) return 0;

    /* cupsCompression: 1, since every row below is PackBits-compressed */
    if (!mp_pwg_u32(e, 1UL)) return 0;

    /* cupsRowCount, cupsRowFeed, cupsRowStep */
    if (!mp_pwg_u32(e, e->height) || !mp_pwg_zeros(e, 4UL * 2UL)) return 0;

    /* cupsNumColors */
    if (!mp_pwg_u32(e, 3UL)) return 0;

    /* cupsBorderlessScalingFactor, cupsPageSize[2], cupsImagingBBox[4]
     * (all float), cupsInteger[16], cupsReal[16] */
    if (!mp_pwg_zeros(e, 4UL * (1UL + 2UL + 4UL + 16UL + 16UL))) return 0;

    /* cupsString[16][64], cupsMarkerType[64], cupsRenderingIntent[64],
     * cupsPageSizeName[64] */
    if (!mp_pwg_zeros(e, 64UL * (16UL + 1UL + 1UL + 1UL))) return 0;

    return !e->failed;
}

/* Encodes one RGB scanline using only the "repeat run" half of PWG/CUPS
 * raster's PackBits-style scheme (control byte 0-127 = "next pixel
 * repeated (byte+1) times", 1-128 pixels). A run of matching pixels
 * compresses normally; any pixel that does not extend a run is simply
 * emitted as its own trivial run of one. This never needs the format's
 * other, more failure-prone "literal run" encoding (byte 129-255, with
 * its own separate minimum-length and byte-value-overflow edge cases)
 * to still be a fully valid, decodable stream - see pwg_writer.h. */
static int mp_pwg_encode_row(MPPwgEncoder *e, const unsigned char *rgb)
{
    unsigned long px = 0;
    unsigned long out = 0;
    unsigned char *scratch = e->scratch;

    if (out >= e->scratch_size) return 0;
    scratch[out++] = 0; /* line-repeat count: this line's data appears once */

    while (px < e->width) {
        const unsigned char *p0 = rgb + px * 3UL;
        unsigned long run = 1;

        while (run < 128UL && (px + run) < e->width) {
            const unsigned char *pn = rgb + (px + run) * 3UL;
            if (pn[0] != p0[0] || pn[1] != p0[1] || pn[2] != p0[2]) break;
            ++run;
        }

        if (out + 1UL + 3UL > e->scratch_size) return 0;
        scratch[out++] = (unsigned char)(run - 1UL);
        scratch[out++] = p0[0];
        scratch[out++] = p0[1];
        scratch[out++] = p0[2];

        px += run;
    }

    return mp_pwg_raw(e, scratch, out);
}

unsigned long mp_pwg_scratch_size(unsigned long width)
{
    if (!width || width > 0x0fffffffUL) return 0;
    /* Worst case: every pixel is its own run (1 control + 3 data bytes),
     * plus the 1-byte line-repeat-count prefix. */
    return width * 4UL + 16UL;
}

int mp_pwg_begin(MPPwgEncoder *e, unsigned long width, unsigned long height,
                 unsigned long page_pts_x, unsigned long page_pts_y,
                 unsigned long dpi,
                 unsigned char *scratch, unsigned long scratch_size,
                 MPPwgWriteFn write_fn, void *write_ctx)
{
    unsigned long need;
    if (!e || !width || !height || width > 65535UL || height > 65535UL ||
        !scratch || !write_fn)
        return 0;
    need = mp_pwg_scratch_size(width);
    if (!need || scratch_size < need) return 0;

    e->width = width;
    e->height = height;
    e->bytes_per_line = width * 3UL;
    e->rows_written = 0;
    e->dpi = dpi ? dpi : 300UL;
    /* Fall back to the pixel-derived size (previous behaviour) only when
     * the caller has no real physical page size to offer. */
    e->page_pts_x = page_pts_x ? page_pts_x : (width * 72UL) / e->dpi;
    e->page_pts_y = page_pts_y ? page_pts_y : (height * 72UL) / e->dpi;
    e->scratch = scratch;
    e->scratch_size = scratch_size;
    e->write_fn = write_fn;
    e->write_ctx = write_ctx;
    e->failed = 0;

    return mp_pwg_write_header(e);
}

int mp_pwg_write_scanline(MPPwgEncoder *e, const unsigned char *rgb)
{
    if (!e || e->failed || !rgb || e->rows_written >= e->height) return 0;
    if (!mp_pwg_encode_row(e, rgb)) {
        e->failed = 1;
        return 0;
    }
    ++e->rows_written;
    return 1;
}

int mp_pwg_finish(MPPwgEncoder *e)
{
    if (!e || e->failed) return 0;
    return e->rows_written == e->height;
}

int mp_pwg_grow(MPPwgEncoder *e, unsigned long extra_rows)
{
    unsigned long new_height;
    if (!e || e->failed || !extra_rows) return 0;
    new_height = e->height + extra_rows;
    if (new_height < e->height || new_height > 65535UL) return 0;
    e->height = new_height;
    return 1;
}
