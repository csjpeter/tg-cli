/**
 * @file test_runner.c
 * @brief Entry point for the integration test suite.
 *
 * Integration tests run against the real Telegram test DC.  They require
 * credentials to be supplied via environment variables (TG_TEST_*).  When the
 * variables are absent the runner prints a clear skip message and exits 0 so
 * that CI pipelines without credentials do not fail.
 *
 * Usage:
 *   tg-integration-test-runner              # run all suites
 *   tg-integration-test-runner <filter>     # run suites matching filter
 *
 * Required environment variables (when actually running tests):
 *   TG_TEST_API_ID    — api_id from my.telegram.org test app (gates the suite)
 *   TG_TEST_API_HASH  — api_hash
 *   TG_TEST_DC_HOST   — test DC hostname or IP (e.g. 149.154.175.10)
 *   TG_TEST_DC_PORT   — test DC port (default: 443)
 *   TG_TEST_RSA_PEM   — test DC RSA public key (PEM, \n-escaped single line)
 *   TG_TEST_PHONE     — pre-registered test phone number
 *   TG_TEST_CODE      — verification code, or "auto" for magic code 12345
 */

#include "test_helpers_integration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Definitions for globals declared in test_helpers.h */
int          g_tests_run    = 0;
int          g_tests_failed = 0;
const char  *g_test_filter  = NULL;

/* Definition for the global declared in test_helpers_integration.h */
integration_config_t g_integration_config;

/* Suite declarations */
void run_integration_smoke_tests(void);
void run_integration_live_tests(void);

/** Run suite_fn when no filter is active or filter is a substring of the name. */
#define RUN_SUITE(suite_fn) do { \
    if (!g_test_filter || strstr(#suite_fn, g_test_filter)) { \
        printf("Running %s...\n", #suite_fn); \
        suite_fn(); \
    } \
} while (0)

/**
 * @brief Load TG_TEST_* environment variables into g_integration_config.
 *
 * getenv() pointers remain valid for the lifetime of the process.
 */
static void load_integration_config(void)
{
    g_integration_config.api_id   = getenv("TG_TEST_API_ID");
    g_integration_config.api_hash = getenv("TG_TEST_API_HASH");
    g_integration_config.dc_host  = getenv("TG_TEST_DC_HOST");
    g_integration_config.rsa_pem  = getenv("TG_TEST_RSA_PEM");
    g_integration_config.phone    = getenv("TG_TEST_PHONE");
    g_integration_config.code     = getenv("TG_TEST_CODE");

    /* Default port when the variable is absent */
    const char *port = getenv("TG_TEST_DC_PORT");
    g_integration_config.dc_port = (port && port[0]) ? port : "443";
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        g_test_filter = argv[1];
    }

    load_integration_config();

    if (!integration_creds_present()) {
        printf("integration tests skipped — set TG_TEST_* env vars to run\n");
        return 0;
    }

    printf("--- Integration Tests (Telegram test DC: %s:%s) ---\n\n",
           g_integration_config.dc_host ? g_integration_config.dc_host : "(unset)",
           g_integration_config.dc_port);

    if (g_test_filter) {
        printf("Filter: \"%s\"\n\n", g_test_filter);
    }

    RUN_SUITE(run_integration_smoke_tests);
    RUN_SUITE(run_integration_live_tests);

    printf("\n--- Integration Test Results ---\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_run - g_tests_failed);
    printf("Tests Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
