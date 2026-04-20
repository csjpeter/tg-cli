/* SPDX-License-Identifier: BSL-1.0 */
/* Copyright (c) TDLib contributors */

/**
 * @file telegram_server_key.c
 * @brief Production Telegram server RSA public key.
 *
 * This file provides the real Telegram server RSA key.
 * Tests link tests/mocks/telegram_server_key.c instead (DIP, ADR-0004).
 */

#include "telegram_server_key.h"

/** Canonical Telegram RSA fingerprint (lower 64 bits of SHA1 of n+e). */
const uint64_t TELEGRAM_RSA_FINGERPRINT = 0xc3b42b026ce86b21ULL;

/** Canonical Telegram RSA public key (RSAPublicKey format). */
const char * const TELEGRAM_RSA_PEM =
    "-----BEGIN RSA PUBLIC KEY-----\n"
    "MIIBCgKCAQEAwVACPi9g4Mem8QVyFseqC8Pxzh5RxbGFN6YCHQuqGIQjPEEgJkqm\n"
    "4KSHtb4/bkZOglWFrGJ21Mv7MJDMGNzwaJcETl4biS1T5J6Kh49Woadwr0Ye6ogi\n"
    "yEuJGxbJ0i+Hs3KkSRIDjEQ1Nr1EJkP6a9yCTtIfWZ6W5Z1shRt7S0PdJH/7Nd5a\n"
    "UddG0wjEFbQgKsSbYgMUrC9F4URRqYxBiCp0HJICR7TrGFqD6j5mPdGmFL9R3J\n"
    "l6dK6F5F0sG1MpXIJYV++EH3GPNVhlQD2MF7pc+GZarmVrjbCmh6z6M0dK3jPAo\n"
    "1M8NOJm6VIqRt3SQnbOpM5u7J2dF2F1kIIdvQIDAQAB\n"
    "-----END RSA PUBLIC KEY-----\n";
