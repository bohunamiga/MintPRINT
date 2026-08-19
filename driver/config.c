/*
 * MintPRINT runtime configuration.
 *
 * Live settings are read from ENV:MintPRINT/Unit0.  If that file does not
 * exist, ENVARC:MintPRINT/Unit0 is used.  Missing or invalid values fall back
 * to the known-good development defaults.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "config.h"

extern struct DosLibrary *DOSBase;

static char g_config_line[192];

static ULONG mp_cfg_len(const char *s)
{
    ULONG n = 0;
    while (s && s[n]) ++n;
    return n;
}

static BOOL mp_cfg_starts(const char *s, const char *prefix)
{
    ULONG i = 0;
    if (!s || !prefix) return FALSE;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return FALSE;
        ++i;
    }
    return TRUE;
}

static BOOL mp_cfg_copy(char *dst, ULONG cap, const char *src)
{
    ULONG n;
    if (!dst || !cap || !src) return FALSE;
    n = mp_cfg_len(src);
    if (n == 0 || n >= cap) return FALSE;
    while (n) {
        dst[n - 1] = src[n - 1];
        --n;
    }
    dst[mp_cfg_len(src)] = 0;
    return TRUE;
}

static void mp_cfg_trim_eol(char *s)
{
    ULONG n = mp_cfg_len(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
                 s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = 0;
    }
}

static ULONG mp_cfg_parse_ulong(const char *s, BOOL *ok)
{
    ULONG v = 0;
    ULONG i = 0;
    BOOL any = FALSE;

    if (ok) *ok = FALSE;
    if (!s) return 0;

    while (s[i] >= '0' && s[i] <= '9') {
        ULONG digit = (ULONG)(s[i] - '0');
        if (v > 429496729UL) return 0;
        v = v * 10UL + digit;
        any = TRUE;
        ++i;
    }

    if (!any || s[i] != 0) return 0;
    if (ok) *ok = TRUE;
    return v;
}

void mp_config_defaults(struct MPConfig *cfg)
{
    if (!cfg) return;

    mp_cfg_copy(cfg->host, sizeof(cfg->host), "192.168.0.51");
    cfg->port = 80;
    mp_cfg_copy(cfg->path, sizeof(cfg->path), "/ipp/print");
    cfg->keep_job = TRUE;
    mp_cfg_copy(cfg->engine, sizeof(cfg->engine), "jpeg");
    cfg->media[0] = 0;
    cfg->source[0] = 0;
    cfg->color[0] = 0;
    cfg->quality[0] = 0;
    cfg->scaling[0] = 0;
    cfg->sides[0] = 0;
}

LONG mp_config_load(struct MPConfig *cfg)
{
    BPTR fh = 0;
    LONG source = MP_CONFIG_SOURCE_DEFAULTS;

    if (!cfg) return MP_CONFIG_SOURCE_DEFAULTS;
    mp_config_defaults(cfg);
    if (!DOSBase) return MP_CONFIG_SOURCE_DEFAULTS;

    fh = Open((CONST_STRPTR)"ENV:MintPRINT/Unit0", MODE_OLDFILE);
    if (fh) {
        source = MP_CONFIG_SOURCE_ENV;
    } else {
        fh = Open((CONST_STRPTR)"ENVARC:MintPRINT/Unit0", MODE_OLDFILE);
        if (fh) source = MP_CONFIG_SOURCE_ENVARC;
    }

    if (!fh) return source;

    while (FGets(fh, g_config_line, sizeof(g_config_line))) {
        const char *value;
        BOOL ok;
        ULONG n;

        mp_cfg_trim_eol(g_config_line);
        if (!g_config_line[0] || g_config_line[0] == '#' ||
            g_config_line[0] == ';') continue;

        if (mp_cfg_starts(g_config_line, "HOST=")) {
            value = g_config_line + 5;
            if (value[0]) mp_cfg_copy(cfg->host, sizeof(cfg->host), value);
            continue;
        }

        if (mp_cfg_starts(g_config_line, "PORT=")) {
            value = g_config_line + 5;
            n = mp_cfg_parse_ulong(value, &ok);
            if (ok && n >= 1UL && n <= 65535UL) cfg->port = (UWORD)n;
            continue;
        }

        if (mp_cfg_starts(g_config_line, "PATH=")) {
            value = g_config_line + 5;
            if (value[0] == '/') mp_cfg_copy(cfg->path, sizeof(cfg->path), value);
            continue;
        }

        if (mp_cfg_starts(g_config_line, "KEEPJOB=")) {
            value = g_config_line + 8;
            cfg->keep_job = (value[0] == '0') ? FALSE : TRUE;
            continue;
        }

        if (mp_cfg_starts(g_config_line, "ENGINE=")) {
            value = g_config_line + 7;
            if (mp_cfg_starts(value, "pwg-raster") && mp_cfg_len(value) == 10) {
                mp_cfg_copy(cfg->engine, sizeof(cfg->engine), "pwg-raster");
            } else {
                mp_cfg_copy(cfg->engine, sizeof(cfg->engine), "jpeg");
            }
            continue;
        }

        if (mp_cfg_starts(g_config_line, "MEDIA=")) {
            value = g_config_line + 6;
            if (value[0]) mp_cfg_copy(cfg->media, sizeof(cfg->media), value);
            continue;
        }
        if (mp_cfg_starts(g_config_line, "SOURCE=")) {
            value = g_config_line + 7;
            if (value[0]) mp_cfg_copy(cfg->source, sizeof(cfg->source), value);
            continue;
        }
        if (mp_cfg_starts(g_config_line, "COLOR=")) {
            value = g_config_line + 6;
            if (value[0]) mp_cfg_copy(cfg->color, sizeof(cfg->color), value);
            continue;
        }
        if (mp_cfg_starts(g_config_line, "QUALITY=")) {
            value = g_config_line + 8;
            if (value[0]) mp_cfg_copy(cfg->quality, sizeof(cfg->quality), value);
            continue;
        }
        if (mp_cfg_starts(g_config_line, "SCALING=")) {
            value = g_config_line + 8;
            if (value[0]) mp_cfg_copy(cfg->scaling, sizeof(cfg->scaling), value);
            continue;
        }
        if (mp_cfg_starts(g_config_line, "SIDES=")) {
            value = g_config_line + 6;
            if (value[0]) mp_cfg_copy(cfg->sides, sizeof(cfg->sides), value);
            continue;
        }
    }

    Close(fh);
    return source;
}
