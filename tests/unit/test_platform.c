/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-03-28 */
/* Tests for platform. */

#include "test_helpers.h"
#include "platform/path.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/wait.h>

void test_platform(void) {

    /* ── platform_home_dir ──────────────────────────────────────────── */

    const char *home = platform_home_dir();
    ASSERT(home != NULL, "platform_home_dir should not return NULL");
    ASSERT(home[0] == '/', "platform_home_dir should return an absolute path");

    /* Must still work when HOME is unset (getpwuid fallback) */
    char saved_home[4096] = {0};
    const char *env_home = getenv("HOME");
    if (env_home) snprintf(saved_home, sizeof(saved_home), "%s", env_home);
    unsetenv("HOME");
    home = platform_home_dir();
    ASSERT(home != NULL, "platform_home_dir should fall back to getpwuid");
    if (saved_home[0]) setenv("HOME", saved_home, 1);

    /* ── platform_cache_dir ─────────────────────────────────────────── */

    /* Default: ~/.cache */
    unsetenv("XDG_CACHE_HOME");
    const char *cache = platform_cache_dir();
    ASSERT(cache != NULL, "platform_cache_dir should not return NULL");
    ASSERT(strstr(cache, ".cache") != NULL,
           "platform_cache_dir default should contain '.cache'");

    /* XDG override */
    setenv("XDG_CACHE_HOME", "/tmp/test-xdg-cache", 1);
    cache = platform_cache_dir();
    ASSERT(cache != NULL, "platform_cache_dir XDG should not return NULL");
    ASSERT(strcmp(cache, "/tmp/test-xdg-cache") == 0,
           "platform_cache_dir should respect XDG_CACHE_HOME");
    unsetenv("XDG_CACHE_HOME");

    /* ── platform_config_dir ────────────────────────────────────────── */

    /* Default: ~/.config */
    unsetenv("XDG_CONFIG_HOME");
    const char *cfg = platform_config_dir();
    ASSERT(cfg != NULL, "platform_config_dir should not return NULL");
    ASSERT(strstr(cfg, ".config") != NULL,
           "platform_config_dir default should contain '.config'");

    /* XDG override */
    setenv("XDG_CONFIG_HOME", "/tmp/test-xdg-config", 1);
    cfg = platform_config_dir();
    ASSERT(cfg != NULL, "platform_config_dir XDG should not return NULL");
    ASSERT(strcmp(cfg, "/tmp/test-xdg-config") == 0,
           "platform_config_dir should respect XDG_CONFIG_HOME");
    unsetenv("XDG_CONFIG_HOME");

}
