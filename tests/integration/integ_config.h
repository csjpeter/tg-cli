/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

#ifndef INTEG_CONFIG_H
#define INTEG_CONFIG_H

/**
 * @file integ_config.h
 * @brief Load integration-test credentials from ~/.config/tg-cli/test.ini.
 *
 * Example test.ini:
 *
 *   [integration]
 *   dc_host     = 149.154.167.40
 *   dc_port     = 443
 *   dc_id       = 0
 *   api_id      = 12345
 *   api_hash    = abc123...
 *   phone       = +99966123456
 *   code        = auto
 *   rsa_pem     = -----BEGIN RSA PUBLIC KEY-----\nMIIBCgKCAQEA...\n-----END RSA PUBLIC KEY-----
 *   session_bin = ~/.config/tg-cli/test-session.bin
 *
 * All fields are optional; absent fields are left as NULL / 0 / default.
 * rsa_pem may use literal \n sequences to embed newlines on a single line.
 * session_bin may start with ~/ which is expanded to $HOME/.
 */

#include "test_helpers_integration.h"

/**
 * @brief Populate *cfg from ~/.config/tg-cli/test.ini.
 * @return 0 on success, -1 if the file does not exist or cannot be read.
 */
int integ_config_load(integration_config_t *cfg);

/**
 * @brief Return the expected path of the config file (heap-allocated).
 * Caller must free().
 */
char *integ_config_path(void);

#endif /* INTEG_CONFIG_H */
