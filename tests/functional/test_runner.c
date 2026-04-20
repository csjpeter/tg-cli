/**
 * @file test_runner.c
 * @brief Entry point for functional tests (linked against real crypto, not mocks).
 *
 * These tests exercise the real OpenSSL crypto implementations, unlike the
 * unit tests which use mock crypto.  They serve as known-answer and round-trip
 * verification to catch divergence between the mock and real behaviour.
 */

#include "test_helpers.h"
#include <stdio.h>
#include <string.h>

int g_tests_run    = 0;
int g_tests_failed = 0;
const char *g_test_filter = NULL;

/* Declarations of test suites */
void run_ige_aes_functional_tests(void);
void run_mtproto_crypto_functional_tests(void);
void run_crypto_kdf_functional_tests(void);
void run_srp_math_functional_tests(void);
void run_srp_roundtrip_functional_tests(void);
void run_tl_skip_message_functional_tests(void);
void run_tl_forward_compat_tests(void);
void run_mt_server_smoke_tests(void);
void run_login_flow_tests(void);
void run_read_path_tests(void);
void run_write_path_tests(void);
void run_upload_download_tests(void);
void run_dialogs_cache_ttl_tests(void);
void run_tui_e2e_tests(void);
void run_send_stdin_tests(void);
void run_dc_session_cache_skip_tests(void);
void run_logout_rpc_tests(void);
void run_wrong_session_id_tests(void);
void run_config_wizard_batch_tests(void);
void run_tg_cli_read_dispatch_tests(void);
void run_logger_lifecycle_tests(void);
void run_session_corruption_tests(void);
void run_config_ini_robustness_tests(void);
void run_resolver_cache_tests(void);
void run_text_rendering_safety_tests(void);
void run_history_rich_metadata_tests(void);

/** Run suite_fn only when no filter is set or the filter is a substring of
 *  the stringified function name. */
#define RUN_SUITE(suite_fn) do { \
    if (!g_test_filter || strstr(#suite_fn, g_test_filter)) { \
        printf("Running %s...\n", #suite_fn); \
        suite_fn(); \
    } \
} while(0)

int main(int argc, char *argv[]) {
    if (argc > 1) {
        g_test_filter = argv[1];
        printf("--- Functional Tests (filter: \"%s\") ---\n\n", g_test_filter);
    }

    RUN_SUITE(run_ige_aes_functional_tests);
    RUN_SUITE(run_mtproto_crypto_functional_tests);
    RUN_SUITE(run_crypto_kdf_functional_tests);
    RUN_SUITE(run_srp_math_functional_tests);
    RUN_SUITE(run_srp_roundtrip_functional_tests);
    RUN_SUITE(run_tl_skip_message_functional_tests);
    RUN_SUITE(run_tl_forward_compat_tests);
    RUN_SUITE(run_mt_server_smoke_tests);
    RUN_SUITE(run_login_flow_tests);
    RUN_SUITE(run_read_path_tests);
    RUN_SUITE(run_write_path_tests);
    RUN_SUITE(run_upload_download_tests);
    RUN_SUITE(run_dialogs_cache_ttl_tests);
    RUN_SUITE(run_tui_e2e_tests);
    RUN_SUITE(run_send_stdin_tests);
    RUN_SUITE(run_dc_session_cache_skip_tests);
    RUN_SUITE(run_logout_rpc_tests);
    RUN_SUITE(run_wrong_session_id_tests);
    RUN_SUITE(run_config_wizard_batch_tests);
    RUN_SUITE(run_tg_cli_read_dispatch_tests);
    RUN_SUITE(run_logger_lifecycle_tests);
    RUN_SUITE(run_session_corruption_tests);
    RUN_SUITE(run_config_ini_robustness_tests);
    RUN_SUITE(run_resolver_cache_tests);
    RUN_SUITE(run_text_rendering_safety_tests);
    RUN_SUITE(run_history_rich_metadata_tests);

    printf("\n--- Functional Test Results ---\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_run - g_tests_failed);
    printf("Tests Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
