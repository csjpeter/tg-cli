/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/config_wizard.h
 * @brief First-run interactive config wizard for api_id / api_hash setup.
 *
 * Available in all three binaries via the `login` subcommand (FEAT-37).
 */

#ifndef APP_CONFIG_WIZARD_H
#define APP_CONFIG_WIZARD_H

/**
 * @brief Run the interactive wizard.
 *
 * Prompts stdin/stdout for api_id and api_hash (with echo suppressed for
 * the hash).  On success writes ~/.config/tg-cli/config.ini atomically with
 * mode 0600.
 *
 * Aborts with an error message if stdin is not a TTY; use
 * config_wizard_run_batch() for non-interactive use.
 *
 * @return 0 on success, -1 on user abort or hard error.
 */
int config_wizard_run_interactive(void);

/**
 * @brief Batch (non-interactive) wizard mode.
 *
 * Validates @p api_id_str and @p api_hash_str, then writes the config file.
 * No prompts are issued.
 *
 * @param api_id_str   Decimal string for the api_id (must be a positive int).
 * @param api_hash_str 32 lowercase hex characters.
 * @param force        If non-zero, overwrite an existing non-empty config.ini.
 * @return 0 on success, -1 on validation or write error.
 */
int config_wizard_run_batch(const char *api_id_str, const char *api_hash_str,
                             int force);

#endif /* APP_CONFIG_WIZARD_H */
