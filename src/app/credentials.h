/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/credentials.h
 * @brief Load api_id/api_hash from environment or config file.
 *
 * Precedence:
 *   1. TG_CLI_API_ID / TG_CLI_API_HASH environment variables.
 *   2. `~/.config/tg-cli/config.ini` lines:
 *        api_id=1234567
 *        api_hash=abcdef...
 *
 * The lookup never reads from stdin; credentials must be provisioned by
 * the operator ahead of time (see docs/SPECIFICATION.md section 5).
 */

#ifndef APP_CREDENTIALS_H
#define APP_CREDENTIALS_H

#include "api_call.h"

/**
 * @brief Load api_id/api_hash into @p out.
 *
 * Writes defaults via `api_config_init` first, then overlays api_id and
 * api_hash from env vars or the config file.
 *
 * @param out Must be non-NULL. Its api_hash is set to an internal static
 *            buffer that remains valid for the process lifetime.
 * @return 0 on success (both values populated), -1 if any required value
 *         is missing.
 */
int credentials_load(ApiConfig *out);

#endif /* APP_CREDENTIALS_H */
