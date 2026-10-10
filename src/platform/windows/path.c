/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-03-28 */
/* Windows home, cache and config directory lookup (untested preparation). */

/* Windows path implementation for MinGW-w64.
 * Uses %USERPROFILE% (home), %LOCALAPPDATA% (cache), %APPDATA% (config). */
#include "../path.h"
#include <stdlib.h>
#include <stdio.h>

static char g_home[4096], g_cache[4096], g_config[4096];

const char *platform_home_dir(void) {
    char *h = getenv("USERPROFILE");
    if (h) { snprintf(g_home, sizeof(g_home), "%s", h); return g_home; }
    return NULL;
}

const char *platform_cache_dir(void) {
    char *h = getenv("LOCALAPPDATA");
    if (h) { snprintf(g_cache, sizeof(g_cache), "%s", h); return g_cache; }
    return NULL;
}

const char *platform_config_dir(void) {
    char *h = getenv("APPDATA");
    if (h) { snprintf(g_config, sizeof(g_config), "%s", h); return g_config; }
    return NULL;
}
