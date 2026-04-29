/* SPDX-License-Identifier: BSL-1.0 */
/* Copyright (c) TDLib contributors */

/**
 * @file telegram_server_key.c
 * @brief Production Telegram server RSA public key.
 *
 * This file provides the real Telegram server RSA key.
 * Tests link tests/mocks/telegram_server_key.c instead (DIP, ADR-0004).
 *
 * FEAT-38: runtime override via telegram_server_key_set_override().
 */

#include "telegram_server_key.h"
#include "crypto.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

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

/* ---- Runtime override (FEAT-38) ---- */

/** Heap-allocated override PEM; NULL means use the compiled-in default. */
static char    *g_override_pem         = NULL;
static uint64_t g_override_fingerprint = 0;

/**
 * Expand literal \\n sequences (two characters: backslash + 'n') to real
 * newline characters.  Returns a heap-allocated string; caller must free.
 */
static char *expand_newlines(const char *src) {
    size_t src_len = strlen(src);
    /* Worst case: no expansion — same length. */
    char *dst = malloc(src_len + 1);
    if (!dst) return NULL;

    size_t di = 0;
    for (size_t si = 0; si < src_len; si++) {
        if (src[si] == '\\' && si + 1 < src_len && src[si + 1] == 'n') {
            dst[di++] = '\n';
            si++; /* skip the 'n' */
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
    return dst;
}

const char *telegram_server_key_get_pem(void) {
    return g_override_pem ? g_override_pem : TELEGRAM_RSA_PEM;
}

uint64_t telegram_server_key_get_fingerprint(void) {
    return g_override_pem ? g_override_fingerprint : TELEGRAM_RSA_FINGERPRINT;
}

int telegram_server_key_set_override(const char *pem) {
    /* NULL → revert to compiled-in defaults. */
    if (!pem) {
        free(g_override_pem);
        g_override_pem = NULL;
        g_override_fingerprint = 0;
        return 0;
    }

    char *expanded = expand_newlines(pem);
    if (!expanded) return -1;

    uint64_t fp = 0;
    if (crypto_rsa_fingerprint(expanded, &fp) != 0) {
        logger_log(LOG_ERROR,
                   "telegram_server_key: rsa_pem in config.ini cannot be "
                   "parsed — override NOT applied");
        free(expanded);
        return -1;
    }

    free(g_override_pem);
    g_override_pem = expanded;
    g_override_fingerprint = fp;

    logger_log(LOG_INFO,
               "telegram_server_key: RSA key override applied "
               "(fingerprint 0x%016llx)",
               (unsigned long long)fp);
    return 0;
}
