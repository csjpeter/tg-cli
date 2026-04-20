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
 */

#ifndef TELEGRAM_SERVER_KEY_H
#define TELEGRAM_SERVER_KEY_H

#include <stdint.h>

/** Telegram server RSA public key fingerprint (lower 64 bits of SHA1). */
extern const uint64_t TELEGRAM_RSA_FINGERPRINT;

/** Telegram server RSA public key in PEM format. */
extern const char * const TELEGRAM_RSA_PEM;

#endif /* TELEGRAM_SERVER_KEY_H */
