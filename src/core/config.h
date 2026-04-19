/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

/**
 * @file config.h
 * @brief Telegram client configuration type shared across all layers.
 */

/** Telegram client configuration. */
typedef struct {
    char *api_base;       /**< Telegram Bot API base URL (default: https://api.telegram.org) */
    char *token;          /**< Bot token or client auth token */
    int   ssl_no_verify;  /**< 1 = disable SSL peer verification (test envs only) */
} Config;

/**
 * @brief Frees all heap-allocated fields of cfg, then frees cfg itself.
 * Safe to call with NULL.
 */
void config_free(Config *cfg);

#endif /* CORE_CONFIG_H */
