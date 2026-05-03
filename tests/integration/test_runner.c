/**
 * @file test_runner.c
 * @brief Entry point for the integration test suite.
 *
 * Integration tests run against the real Telegram test DC.  Credentials are
 * loaded from ~/.config/tg-cli/test.ini.  When the file is absent the runner
 * prints a clear skip message and exits 0 so that CI pipelines without
 * credentials do not fail.
 *
 * Usage:
 *   tg-integration-test-runner              # run all suites
 *   tg-integration-test-runner <filter>     # run suites matching filter
 *
 * Configuration file: ~/.config/tg-cli/test.ini
 * Run `./manage.sh test-login` once to set up credentials and save a session.
 */

#include "integ_config.h"
#include "test_helpers_integration.h"
#include "app/dc_config.h"
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

int main(int argc, char *argv[])
{
    if (argc > 1) {
        g_test_filter = argv[1];
    }

    memset(&g_integration_config, 0, sizeof(g_integration_config));

    if (integ_config_load(&g_integration_config) != 0) {
        char *path = integ_config_path();
        printf("integration tests skipped — %s not found\n"
               "  Run: ./manage.sh test-login   to set up credentials\n",
               path ? path : "~/.config/tg-cli/test.ini");
        free(path);
        return 0;
    }

    if (!integration_creds_present()) {
        char *path = integ_config_path();
        printf("integration tests skipped — api_id missing in %s\n",
               path ? path : "~/.config/tg-cli/test.ini");
        free(path);
        return 0;
    }

    printf("--- Integration Tests (Telegram test DC: %s:%s) ---\n\n",
           g_integration_config.dc_host ? g_integration_config.dc_host : "(unset)",
           g_integration_config.dc_port ? g_integration_config.dc_port : "443");

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
