#ifndef MINTPRINT_CONFIG_H
#define MINTPRINT_CONFIG_H

#include <exec/types.h>

#define MP_CONFIG_HOST_MAX 64
#define MP_CONFIG_PATH_MAX 96
#define MP_CONFIG_OPTION_MAX 64
#define MP_CONFIG_ENGINE_MAX 16

#define MP_CONFIG_SOURCE_DEFAULTS 0
#define MP_CONFIG_SOURCE_ENV      1
#define MP_CONFIG_SOURCE_ENVARC   2

struct MPConfig {
    char host[MP_CONFIG_HOST_MAX];
    UWORD port;
    char path[MP_CONFIG_PATH_MAX];
    BOOL keep_job;
    char engine[MP_CONFIG_ENGINE_MAX];
    char media[MP_CONFIG_OPTION_MAX];
    char source[MP_CONFIG_OPTION_MAX];
    char color[MP_CONFIG_OPTION_MAX];
    char quality[MP_CONFIG_OPTION_MAX];
    char scaling[MP_CONFIG_OPTION_MAX];
    char sides[MP_CONFIG_OPTION_MAX];
};

void mp_config_defaults(struct MPConfig *cfg);
LONG mp_config_load(struct MPConfig *cfg);

#endif
