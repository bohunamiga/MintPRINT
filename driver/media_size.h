#ifndef MINTPRINT_MEDIA_SIZE_H
#define MINTPRINT_MEDIA_SIZE_H

/* Parse the dimensions encoded in a PWG media keyword. Dimensions are
 * returned in hundredths of a millimetre, matching IPP media-size. */
int mp_media_dimensions_100mm(const char *media,
                              unsigned long *x,
                              unsigned long *y);

/* Convert a PWG media keyword to PostScript points and orient the physical
 * sheet to match the raster supplied by printer.device. This keeps the
 * document's /PageSize on a real supported medium while allowing landscape
 * application output. Returns zero when the media keyword is unknown. */
int mp_media_page_points(const char *media,
                         unsigned long raster_width,
                         unsigned long raster_height,
                         unsigned long *page_width,
                         unsigned long *page_height);

/* Infer the complete raster height for the configured media from the width
 * printer.device actually supplied. The closest media edge determines
 * portrait versus landscape, while using the real raster width preserves
 * any small printable-area rounding difference (2478 rather than 2480 for
 * A4 at 300 DPI, for example). Returns zero for an unknown media keyword. */
unsigned long mp_media_target_height(const char *media,
                                     unsigned long width_px,
                                     unsigned long dpi);

/* Wordworth can finish a NOFORMFEED page with a four-pixel auxiliary dump.
 * It is not a new physical page and cannot be appended as horizontal rows to
 * the already-streamed full-width PWG raster. Keep the classifier narrow so
 * ordinary differently-sized pages are never swallowed. */
int mp_is_tiny_auxiliary_band(unsigned long page_width,
                              unsigned long band_width);

/* Decide whether the logical vertical extent accumulated under
 * SPECIAL_NOFORMFEED has reached the configured physical media height.
 * Auxiliary dumps are not written into the full-width PWG raster, but their
 * height still contributes useful page-boundary evidence. A zero target
 * means the media was unknown, so no automatic boundary is inferred. */
int mp_media_page_complete(unsigned long raster_rows,
                           unsigned long auxiliary_rows,
                           unsigned long target_rows);

#endif
