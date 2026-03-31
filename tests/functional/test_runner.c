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

int main(void) {
    run_ige_aes_functional_tests();
    run_mtproto_crypto_functional_tests();

    printf("\n--- Functional Test Results ---\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_run - g_tests_failed);
    printf("Tests Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
