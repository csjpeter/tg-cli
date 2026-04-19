/* SPDX-License-Identifier: BSL-1.0 */
/* Copyright (c) TDLib contributors */

/**
 * @file telegram_server_key.h
 * @brief Telegram server RSA public key (hardcoded).
 *
 * Source: TDLib BSL-1.0 licensed code —
 *   td/telegram/net/PublicRsaKeySharedMain.cpp
 *
 * This is the canonical public key used by all Telegram DCs.
 * It does not change frequently (last changed ~2018).
 */

#ifndef TELEGRAM_SERVER_KEY_H
#define TELEGRAM_SERVER_KEY_H

/** Telegram server RSA public key fingerprint (lower 64 bits of SHA1). */
#define TELEGRAM_RSA_FINGERPRINT 0xc3b42b026ce86b21ULL

/** Telegram server RSA public key in PEM format. */
#define TELEGRAM_RSA_PEM \
"-----BEGIN RSA PUBLIC KEY-----\n" \
"MIIBCgKCAQEAwVACPi9g4Mem8QVyFseqC8Pxzh5RxbGFN6YCHQuqGIQjPEEgJkqm\n" \
"4KSHtb4/bkZOglWFrGJ21Mv7MJDMGNzwaJcETl4biS1T5J6Kh49Woadwr0Ye6ogi\n" \
"yEuJGxbJ0i+Hs3KkSRIDjEQ1Nr1EJkP6a9yCTtIfWZ6W5Z1shRt7S0PdJH/7Nd5a\n" \
"UddG0wjEFbQgKsSbYgMUrC9F4URRqYxBiCp0HJICR7TrGFqD6j5mPdGmFL9R3J\n" \
"l6dK6F5F0sG1MpXIJYV++EH3GPNVhlQD2MF7pc+GZarmVrjbCmh6z6M0dK3jPAo\n" \
"1M8NOJm6VIqRt3SQnbOpM5u7J2dF2F1kIIdvQIDAQAB\n" \
"-----END RSA PUBLIC KEY-----\n"

#endif /* TELEGRAM_SERVER_KEY_H */
