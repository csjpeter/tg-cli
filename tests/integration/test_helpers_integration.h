/**
 * @file test_helpers_integration.h
 * @brief Helpers for integration tests against the real Telegram test DC.
 *
 * Integration tests require TG_TEST_* environment variables to be set.
 * When they are absent, use SKIP_IF_NO_CREDS() at the top of each suite
 * function to exit cleanly.
 */
#ifndef TEST_HELPERS_INTEGRATION_H
#define TEST_HELPERS_INTEGRATION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_helpers.h"

/**
 * Aggregates all credential/connection values read from the environment.
 * Populated once by integration_config_load() in test_runner.c.
 */
typedef struct {
    const char *dc_host;   /**< TG_TEST_DC_HOST  — test DC hostname or IP */
    const char *dc_port;   /**< TG_TEST_DC_PORT  — test DC port (default "443") */
    int         dc_id;     /**< TG_TEST_DC_ID    — DC id to use (default: DEFAULT_DC_ID) */
    const char *rsa_pem;   /**< TG_TEST_RSA_PEM  — test DC RSA public key (PEM) */
    const char *api_id;    /**< TG_TEST_API_ID   — api_id from test app */
    const char *api_hash;  /**< TG_TEST_API_HASH — api_hash from test app */
    const char *phone;     /**< TG_TEST_PHONE    — pre-registered test phone */
    const char *code;      /**< TG_TEST_CODE     — SMS code or "auto" (magic 12345) */
} integration_config_t;

/** Global config populated by test_runner.c before any suite runs. */
extern integration_config_t g_integration_config;

/**
 * @brief Return true when all mandatory credentials are present.
 *
 * TG_TEST_API_ID is treated as the gating variable: if it is absent the
 * whole suite is considered unconfigured and should be skipped.
 */
static inline int integration_creds_present(void)
{
    return g_integration_config.api_id != NULL
        && g_integration_config.api_id[0] != '\0';
}

/**
 * Call at the top of every integration suite function.
 * Prints a SKIP notice and returns from the *calling function* when no
 * credentials are configured.
 */
#define SKIP_IF_NO_CREDS() do { \
    if (!integration_creds_present()) { \
        printf("  [SKIP] %s — TG_TEST_* credentials not set\n", __func__); \
        return; \
    } \
} while (0)

#endif /* TEST_HELPERS_INTEGRATION_H */
