/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/bootstrap.c
 * @brief Shared startup path: directories, logger.
 */

#include "app/bootstrap.h"

#include "logger.h"
#include "fs_util.h"
#include "platform/path.h"

#include <stdio.h>
#include <string.h>

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
    return 0;
}

void app_shutdown(AppContext *ctx) {
    (void)ctx;
    logger_log(LOG_INFO, "shutdown");
    logger_close();
}
