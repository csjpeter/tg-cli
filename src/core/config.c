/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

#include "config.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Frees all heap-allocated fields of cfg, then frees cfg itself.
 *
 * Sensitive fields (`token`, `api_base`) are wiped with `explicit_bzero`
 * before `free()` so the credential cannot be recovered from a post-mortem
 * core dump or heap inspection (QA-17). `explicit_bzero` is a GNU extension
 * enabled via the project-wide `_GNU_SOURCE` definition in CMakeLists.txt.
 */
void config_free(Config *cfg) {
    if (!cfg) return;
    if (cfg->api_base) explicit_bzero(cfg->api_base, strlen(cfg->api_base));
    free(cfg->api_base);
    if (cfg->token) explicit_bzero(cfg->token, strlen(cfg->token));
    free(cfg->token);
    free(cfg);
}
