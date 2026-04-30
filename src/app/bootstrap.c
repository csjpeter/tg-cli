/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/bootstrap.c
 * @brief Shared startup path: directories, logger, config overrides (FEAT-38).
 */

#include "app/bootstrap.h"
#include "app/dc_config.h"

#include "logger.h"
#include "fs_util.h"
#include "platform/path.h"
#include "telegram_server_key.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BOOTSTRAP_CONFIG_SUBDIR "tg-cli"
#define BOOTSTRAP_CONFIG_FILE   "config.ini"

/**
 * Read the value of @p key from INI file at @p path into @p out (cap @p cap).
 * Returns 0 if found and non-empty, -1 otherwise.
 * Respects comment lines (# and ;), trims whitespace, last occurrence wins.
 */
static int bootstrap_read_ini_key(const char *path, const char *key,
                                  char *out, size_t cap) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[2048]; /* long enough for a full PEM in a single key */
    size_t klen = strlen(key);
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r' ||
            *p == '#'  || *p == ';') continue;
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
        /* rtrim */
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                         out[n-1] == ' '  || out[n-1] == '\t')) {
            out[--n] = '\0';
        }
        found = (n > 0);
    }
    fclose(fp);
    return found ? 0 : -1;
}

/**
 * Apply DC-host and RSA-key overrides from config.ini (FEAT-38).
 * Missing keys are silently ignored.
 */
static void apply_config_overrides(const char *config_dir) {
    if (!config_dir) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s",
             config_dir, BOOTSTRAP_CONFIG_SUBDIR, BOOTSTRAP_CONFIG_FILE);

    /* rsa_pem override. */
    char rsa_buf[4096];
    if (bootstrap_read_ini_key(path, "rsa_pem", rsa_buf, sizeof(rsa_buf)) == 0) {
        if (telegram_server_key_set_override(rsa_buf) != 0) {
            logger_log(LOG_WARN,
                       "bootstrap: rsa_pem in config.ini is invalid");
        }
    }

    /* dc_N_host overrides (1..5). */
    for (int id = 1; id <= 5; id++) {
        char key[16];
        snprintf(key, sizeof(key), "dc_%d_host", id);
        char host_buf[256];
        if (bootstrap_read_ini_key(path, key, host_buf, sizeof(host_buf)) == 0) {
            dc_config_set_host_override(id, host_buf);
            logger_log(LOG_INFO,
                       "bootstrap: DC %d host overridden to %s", id, host_buf);
        }
    }
}

int app_bootstrap(AppContext *ctx, const char *program_name) {
    if (!ctx || !program_name) return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->cache_dir  = platform_cache_dir();
    ctx->config_dir = platform_config_dir();

    if (ctx->cache_dir)  fs_mkdir_p(ctx->cache_dir, 0700);
    if (ctx->config_dir) fs_mkdir_p(ctx->config_dir, 0700);

    const char *base = ctx->cache_dir ? ctx->cache_dir : "/tmp/tg-cli";
    snprintf(ctx->log_path, sizeof(ctx->log_path), "%s/logs", base);
    fs_mkdir_p(ctx->log_path, 0700);

    size_t dir_len = strlen(ctx->log_path);
    snprintf(ctx->log_path + dir_len, sizeof(ctx->log_path) - dir_len,
             "/%s.log", program_name);

    logger_init(ctx->log_path, LOG_INFO);
    logger_log(LOG_INFO, "%s starting", program_name);

    /* Apply DC-host and RSA-key overrides from config.ini. */
    apply_config_overrides(ctx->config_dir);

    if (!telegram_server_key_get_pem()) {
        logger_log(LOG_ERROR,
                   "No RSA public key configured. "
                   "Add rsa_pem = <key> to ~/.config/tg-cli/config.ini "
                   "(obtain your api_id, api_hash, and RSA key at https://my.telegram.org)");
        return -1;
    }

    return 0;
}

void app_shutdown(AppContext *ctx) {
    (void)ctx;
    logger_log(LOG_INFO, "shutdown");
    logger_close();
}
