/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/credentials.c
 * @brief Env + INI-file api_id/api_hash loader.
 */

#include "app/credentials.h"

#include "logger.h"
#include "platform/path.h"
#include "raii.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_HASH_MAX 64

static char g_api_hash_buf[API_HASH_MAX + 1];

static void trim_inplace(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

/** Looks for `key=value` in the INI and writes value into @p out (copied). */
static int read_ini_key(const char *path, const char *key,
                        char *out, size_t cap) {
    RAII_FILE FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, klen) != 0) continue;
        p += klen;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;

        size_t n = strlen(p);
        if (n >= cap) n = cap - 1;
        memcpy(out, p, n);
        out[n] = '\0';
        trim_inplace(out);
        return 0;
    }
    return -1;
}

int credentials_load(ApiConfig *out) {
    if (!out) return -1;
    api_config_init(out);

    /* -- api_id -- */
    int api_id = 0;
    const char *env_id = getenv("TG_CLI_API_ID");
    if (env_id && *env_id) {
        api_id = atoi(env_id);
    }

    /* -- api_hash from env -- */
    const char *env_hash = getenv("TG_CLI_API_HASH");
    if (env_hash && *env_hash) {
        size_t n = strlen(env_hash);
        if (n > API_HASH_MAX) n = API_HASH_MAX;
        memcpy(g_api_hash_buf, env_hash, n);
        g_api_hash_buf[n] = '\0';
    } else {
        g_api_hash_buf[0] = '\0';
    }

    /* -- Fall back to ~/.config/tg-cli/config.ini for missing values -- */
    if (api_id == 0 || g_api_hash_buf[0] == '\0') {
        const char *cfg_dir = platform_config_dir();
        if (cfg_dir) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/tg-cli/config.ini", cfg_dir);

            if (api_id == 0) {
                char buf[32];
                if (read_ini_key(path, "api_id", buf, sizeof(buf)) == 0) {
                    api_id = atoi(buf);
                }
            }
            if (g_api_hash_buf[0] == '\0') {
                read_ini_key(path, "api_hash",
                             g_api_hash_buf, sizeof(g_api_hash_buf));
            }
        }
    }

    if (api_id == 0 || g_api_hash_buf[0] == '\0') {
        logger_log(LOG_ERROR,
                   "credentials: api_id/api_hash not found. Set "
                   "TG_CLI_API_ID and TG_CLI_API_HASH env vars, or add "
                   "api_id=/api_hash= lines to ~/.config/tg-cli/config.ini");
        return -1;
    }

    out->api_id = api_id;
    out->api_hash = g_api_hash_buf;
    return 0;
}
