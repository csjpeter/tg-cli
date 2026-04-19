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

int g_tests_run    = 0;
int g_tests_failed = 0;

/* Declarations of test suites */
void run_ige_aes_functional_tests(void);
void run_mtproto_crypto_functional_tests(void);
void run_crypto_kdf_functional_tests(void);
void run_srp_math_functional_tests(void);
void run_srp_roundtrip_functional_tests(void);
void run_tl_skip_message_functional_tests(void);
void run_mt_server_smoke_tests(void);
void run_login_flow_tests(void);
void run_read_path_tests(void);
void run_write_path_tests(void);
void run_upload_download_tests(void);
void run_dialogs_cache_ttl_tests(void);
void run_tui_e2e_tests(void);
void run_send_stdin_tests(void);
void run_dc_session_cache_skip_tests(void);

int main(void) {
    run_ige_aes_functional_tests();
    run_mtproto_crypto_functional_tests();
    run_crypto_kdf_functional_tests();
    run_srp_math_functional_tests();
    run_srp_roundtrip_functional_tests();
    run_tl_skip_message_functional_tests();
    run_mt_server_smoke_tests();
    run_login_flow_tests();
    run_read_path_tests();
    run_write_path_tests();
    run_upload_download_tests();
    run_dialogs_cache_ttl_tests();
    run_tui_e2e_tests();
    run_send_stdin_tests();
    run_dc_session_cache_skip_tests();

    printf("\n--- Functional Test Results ---\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_run - g_tests_failed);
    printf("Tests Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
