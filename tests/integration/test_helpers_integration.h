/**
 * @file test_helpers_integration.h
 * @brief Helpers for integration tests against the real Telegram test DC.
 *
 * Credentials are loaded from ~/.config/tg-cli/test.ini by integ_config_load().
 * Use SKIP_IF_NO_CREDS() at the top of each suite function to exit cleanly
 * when no credentials are configured.
 */
#ifndef TEST_HELPERS_INTEGRATION_H
#define TEST_HELPERS_INTEGRATION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "test_helpers.h"

/**
 * All credential/connection values loaded from ~/.config/tg-cli/test.ini.
 * Populated once by integ_config_load() before any suite runs.
 * All string fields are heap-allocated (or NULL when absent).
 */
typedef struct {
    char *dc_host;    /**< dc_host    — test DC hostname or IP */
    char *dc_port;    /**< dc_port    — test DC port (default "443") */
    int   dc_id;      /**< dc_id      — DC id to embed in DH (default DEFAULT_DC_ID) */
    char *rsa_pem;    /**< rsa_pem    — test DC RSA public key (PEM) */
    char *api_id;     /**< api_id     — numeric api_id from test app */
    char *api_hash;   /**< api_hash   — api_hash from test app */
    char *phone;      /**< phone      — pre-registered test phone number */
    char *code;       /**< code       — SMS code or "auto" (magic 12345) */
    char *session_bin;/**< session_bin — pre-saved session file for fast-path login */
} integration_config_t;

/** Global config populated before any suite runs. */
extern integration_config_t g_integration_config;

/**
 * @brief Return true when the minimum mandatory credentials are present.
 * api_id acts as the gate: if absent, the whole suite is skipped.
 */
static inline int integration_creds_present(void)
{
    return g_integration_config.api_id != NULL
        && g_integration_config.api_id[0] != '\0';
}

/**
 * Call at the top of every integration suite function.
 * Prints a SKIP notice and returns from the calling function when no
 * credentials are configured.
 */
#define SKIP_IF_NO_CREDS() do { \
    if (!integration_creds_present()) { \
        printf("  [SKIP] %s — no credentials in test.ini\n", __func__); \
        return; \
    } \
} while (0)

/**
 * @brief Return true when a login is possible without interactive input.
 *
 * Login is possible if api_id is set AND either:
 *   - a saved session file exists at session_bin, OR
 *   - a phone number is configured (new login via sendCode/signIn).
 */
static inline int integration_can_login(void)
{
    if (!integration_creds_present()) return 0;
    /* Only a saved session file allows automated (non-interactive) login.
     * A phone number alone is insufficient — the code arrives interactively. */
    if (g_integration_config.session_bin && g_integration_config.session_bin[0]) {
        struct stat _st;
        if (stat(g_integration_config.session_bin, &_st) == 0) return 1;
    }
    return 0;
}

/**
 * Call at the top of every test that requires an authenticated session.
 * Skips when no saved session and no phone are configured, printing a hint
 * about the one-time setup step.
 */
#define SKIP_IF_CANT_LOGIN() do { \
    if (!integration_can_login()) { \
        printf("  [SKIP] %s — no session or phone configured\n" \
               "         run: ./manage.sh test-login  (one-time setup)\n", \
               __func__); \
        return; \
    } \
} while (0)

#endif /* TEST_HELPERS_INTEGRATION_H */
