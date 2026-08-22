#ifndef MINTPRINT_DPI_OPTIONS_H
#define MINTPRINT_DPI_OPTIONS_H

#define MP_DPI_MAX_OPTIONS 2

struct MPDpiOptions {
    int values[MP_DPI_MAX_OPTIONS];
    int compatibility[MP_DPI_MAX_OPTIONS];
    int count;
    int active;
    int selected;
};

/* Builds the DPI choices shown by Settings without changing the printer's
 * reported capability list. PWG Raster gets a marked 300-DPI compatibility
 * choice when a printer reports resolutions but omits 300 DPI. A saved or
 * explicitly selected 300-DPI value may remain active; otherwise a reported
 * resolution stays the default. */
void mp_dpi_build_options(const int *reported, int reported_count,
                          int pwg_raster, int requested,
                          int requested_explicit,
                          struct MPDpiOptions *out);

#endif
