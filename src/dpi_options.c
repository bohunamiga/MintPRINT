#include "dpi_options.h"

static int mp_dpi_valid(int dpi)
{
    return dpi == 300 || dpi == 600;
}

static int mp_dpi_find(const struct MPDpiOptions *options, int dpi)
{
    int i;

    for (i = 0; i < options->count; ++i) {
        if (options->values[i] == dpi) return i;
    }
    return -1;
}

void mp_dpi_build_options(const int *reported, int reported_count,
                          int pwg_raster, int requested,
                          int requested_explicit,
                          struct MPDpiOptions *out)
{
    int i;
    int requested_index;
    int reported_300 = 0;

    if (!out) return;

    out->count = 0;
    out->active = 0;
    out->selected = 300;
    for (i = 0; i < MP_DPI_MAX_OPTIONS; ++i) {
        out->values[i] = 0;
        out->compatibility[i] = 0;
    }

    if (reported && reported_count > 0) {
        for (i = 0; i < reported_count; ++i) {
            int dpi = reported[i];
            if (!mp_dpi_valid(dpi) ||
                mp_dpi_find(out, dpi) >= 0 ||
                out->count >= MP_DPI_MAX_OPTIONS)
                continue;
            out->values[out->count] = dpi;
            out->compatibility[out->count] = 0;
            if (dpi == 300) reported_300 = 1;
            ++out->count;
        }
    }

    if (pwg_raster && out->count > 0 && !reported_300 &&
        out->count < MP_DPI_MAX_OPTIONS) {
        out->values[out->count] = 300;
        out->compatibility[out->count] = 1;
        ++out->count;
    }

    /* No capabilities: retain the historic disabled 300-DPI placeholder. */
    if (out->count == 0) {
        out->values[0] = 300;
        out->compatibility[0] = 0;
        out->count = 1;
        return;
    }

    requested_index = mp_dpi_find(out, requested);
    if (requested_index >= 0 &&
        (!out->compatibility[requested_index] || requested_explicit)) {
        out->active = requested_index;
    } else {
        /* Prefer an actually reported 300-DPI mode, then the first reported
         * resolution. Never auto-select the unreported compatibility mode. */
        out->active = 0;
        for (i = 0; i < out->count; ++i) {
            if (!out->compatibility[i] && out->values[i] == 300) {
                out->active = i;
                break;
            }
        }
    }
    out->selected = out->values[out->active];
}
