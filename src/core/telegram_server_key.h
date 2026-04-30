/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file telegram_server_key.h
 * @brief Telegram server RSA public key — runtime-supplied via config.ini.
 *
 * Every user must obtain their own api_id / api_hash and RSA public key from
 * Telegram (https://my.telegram.org) and record them in config.ini.  There is
 * no compiled-in fallback key: telegram_server_key_get_pem() returns NULL and
 * telegram_server_key_get_fingerprint() returns 0 until
 * telegram_server_key_set_override() has been called successfully.
 *
 * The values are defined in a separate translation unit so that tests can link
 * a different implementation (tests/mocks/telegram_server_key.c) without
 * recompiling production code.  See ADR-0004 (DIP pattern).
 */

#ifndef TELEGRAM_SERVER_KEY_H
#define TELEGRAM_SERVER_KEY_H

#include <stdint.h>

/**
 * @brief Return the active RSA PEM string.
 * @return Non-NULL PEM string after a successful telegram_server_key_set_override()
 *         call; NULL if no key has been configured yet.
 */
const char *telegram_server_key_get_pem(void);

/**
 * @brief Return the active RSA fingerprint (lower 64 bits of SHA1).
 * @return Non-zero fingerprint after a successful telegram_server_key_set_override()
 *         call; 0 if no key has been configured yet.
 */
uint64_t telegram_server_key_get_fingerprint(void);

/**
 * @brief Supply the RSA public key at runtime.
 *
 * Computes the SHA1 fingerprint from @p pem and stores both values for the
 * lifetime of the process.  Subsequent calls to telegram_server_key_get_pem()
 * and telegram_server_key_get_fingerprint() return these values.
 *
 * @param pem  PEM string (NULL-terminated).  Embedded @c \\n escape sequences
 *             (as written in config.ini) are expanded to real newlines before
 *             storing.  Pass NULL to clear the override (get_pem returns NULL).
 * @return 0 on success, -1 if the PEM cannot be parsed (override NOT applied).
 */
int telegram_server_key_set_override(const char *pem);

#endif /* TELEGRAM_SERVER_KEY_H */
