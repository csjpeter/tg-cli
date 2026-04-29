/* SPDX-License-Identifier: BSL-1.0 */
/* Copyright (c) TDLib contributors */

/**
 * @file telegram_server_key.h
 * @brief Telegram server RSA public key (runtime-selectable via DIP).
 *
 * Source: TDLib BSL-1.0 licensed code —
 *   td/telegram/net/PublicRsaKeySharedMain.cpp
 *
 * This is the canonical public key used by all Telegram DCs.
 * It does not change frequently (last changed ~2018).
 *
 * The values are defined in a separate translation unit so that tests
 * can link a different implementation (tests/mocks/telegram_server_key.c)
 * without recompiling production code. See ADR-0004 (DIP pattern).
 *
 * Runtime override (FEAT-38):
 *   Call telegram_server_key_set_override() once at startup (after config.ini
 *   is loaded) to replace both the PEM and its pre-computed fingerprint.
 *   Pass NULL to revert to the compiled-in defaults.
 */

#ifndef TELEGRAM_SERVER_KEY_H
#define TELEGRAM_SERVER_KEY_H

#include <stdint.h>

/** Telegram server RSA public key fingerprint (lower 64 bits of SHA1). */
extern const uint64_t TELEGRAM_RSA_FINGERPRINT;

/** Telegram server RSA public key in PEM format. */
extern const char * const TELEGRAM_RSA_PEM;

/**
 * @brief Return the active RSA PEM (override if set, else compiled-in default).
 * @return Non-NULL PEM string.
 */
const char *telegram_server_key_get_pem(void);

/**
 * @brief Return the active RSA fingerprint (override if set, else compiled-in).
 * @return 64-bit fingerprint.
 */
uint64_t telegram_server_key_get_fingerprint(void);

/**
 * @brief Override the RSA key used at runtime (FEAT-38).
 *
 * Computes the SHA1 fingerprint from @p pem and stores both values for
 * the lifetime of the process.  Subsequent calls to
 * telegram_server_key_get_pem() / telegram_server_key_get_fingerprint()
 * return the overridden values.
 *
 * @param pem  PEM string (NULL-terminated).  Embedded @c \\n escape sequences
 *             (as written in config.ini) are expanded to real newlines before
 *             storing.  Pass NULL to revert to the compiled-in defaults.
 * @return 0 on success, -1 if the PEM cannot be parsed to extract the
 *         fingerprint (override is NOT applied in that case).
 */
int telegram_server_key_set_override(const char *pem);

#endif /* TELEGRAM_SERVER_KEY_H */
