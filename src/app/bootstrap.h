/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/bootstrap.h
 * @brief Shared runtime bootstrap: directories, logger init, config load.
 *
 * Used by all binaries (tg-cli-ro, tg-tui, tg-cli). Keeps main/... tiny.
 */

#ifndef APP_BOOTSTRAP_H
#define APP_BOOTSTRAP_H

#include <stddef.h>

/** @brief Runtime context shared across the binary lifetime. */
typedef struct {
    const char *cache_dir;    /**< Resolved cache directory (platform).  */
    const char *config_dir;   /**< Resolved config directory (platform). */
    char        log_path[2048]; /**< Full path to the active log file.   */
} AppContext;

/**
 * @brief Initialise directories and logger.
 *
 * @param ctx Output context filled on success.
 * @param program_name Binary name, e.g. "tg-cli-ro" — used in log file name.
 * @return 0 on success, -1 on unrecoverable error.
 */
int app_bootstrap(AppContext *ctx, const char *program_name);

/** @brief Flush logs and release resources. */
void app_shutdown(AppContext *ctx);

#endif /* APP_BOOTSTRAP_H */
