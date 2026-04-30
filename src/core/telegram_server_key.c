/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file telegram_server_key.c
 * @brief Runtime RSA public key store — no compiled-in default.
 *
 * The application refuses to start unless the user has provided an rsa_pem
 * entry in config.ini.  See telegram_server_key.h for details.
 *
 * Tests link tests/mocks/telegram_server_key.c instead (DIP, ADR-0004).
 */

#include "telegram_server_key.h"
#include "crypto.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

static char    *g_pem         = NULL;
static uint64_t g_fingerprint = 0;

/**
 * Expand literal \\n sequences (backslash + 'n') to real newline characters.
 * Returns a heap-allocated string; caller must free.
 */
static char *expand_newlines(const char *src) {
    size_t src_len = strlen(src);
    char *dst = malloc(src_len + 1);
    if (!dst) return NULL;

    size_t di = 0;
    for (size_t si = 0; si < src_len; si++) {
        if (src[si] == '\\' && si + 1 < src_len && src[si + 1] == 'n') {
            dst[di++] = '\n';
            si++;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
    return dst;
}

const char *telegram_server_key_get_pem(void) {
    return g_pem;
}

uint64_t telegram_server_key_get_fingerprint(void) {
    return g_fingerprint;
}

int telegram_server_key_set_override(const char *pem) {
    if (!pem) {
        free(g_pem);
        g_pem = NULL;
        g_fingerprint = 0;
        return 0;
    }

    char *expanded = expand_newlines(pem);
    if (!expanded) return -1;

    uint64_t fp = 0;
    if (crypto_rsa_fingerprint(expanded, &fp) != 0) {
        logger_log(LOG_ERROR,
                   "telegram_server_key: rsa_pem cannot be parsed — "
                   "check the rsa_pem entry in config.ini");
        free(expanded);
        return -1;
    }

    free(g_pem);
    g_pem = expanded;
    g_fingerprint = fp;

    logger_log(LOG_INFO,
               "telegram_server_key: RSA key loaded (fingerprint 0x%016llx)",
               (unsigned long long)fp);
    return 0;
}
