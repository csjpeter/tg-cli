#include <stdio.h>
#include <stdlib.h>
#include "test_helpers.h"
#include "logger.h"

// Globals defined in test_helpers.h
int g_tests_run = 0;
int g_tests_failed = 0;
const char *g_test_filter = NULL;

// Forward declarations of test suites
void test_fs_util(void);
void test_config_store(void);
void test_logger(void);
void test_cache_store(void);
void test_platform(void);
void test_tl_serial(void);
void run_tl_skip_tests(void);
void test_ige(void);
void test_mtproto_crypto(void);
void test_phase2(void);
void test_rpc(void);
void test_auth(void);
void test_gzip(void);
void test_registry(void);
void test_api_call(void);
void run_auth_session_tests(void);
void run_auth_2fa_tests(void);
void run_auth_logout_tests(void);
void run_arg_parse_tests(void);
void run_watch_json_tests(void);
void run_readline_tests(void);
void run_domain_self_tests(void);
void run_domain_dialogs_tests(void);
void run_domain_history_tests(void);
void run_domain_updates_tests(void);
void run_domain_user_info_tests(void);
void run_domain_search_tests(void);
void run_domain_contacts_tests(void);
void run_domain_media_tests(void);
void run_domain_send_tests(void);
void run_domain_read_history_tests(void);
void run_domain_edit_delete_forward_tests(void);
void run_domain_upload_tests(void);
void run_dc_config_tests(void);
void run_session_store_tests(void);
void run_dc_session_tests(void);
void run_auth_transfer_tests(void);
void run_updates_state_store_tests(void);
void run_media_index_tests(void);
void test_tui_screen_run(void);
void test_tui_pane_run(void);
void test_tui_list_view_run(void);
void test_tui_dialog_pane_run(void);
void test_tui_history_pane_run(void);
void test_tui_status_row_run(void);
void test_tui_app_run(void);

int main(int argc, char *argv[]) {
    if (argc > 1) {
        g_test_filter = argv[1];
        printf("--- tg-cli Unit Test Suite (filter: \"%s\") ---\n\n", g_test_filter);
    } else {
        printf("--- tg-cli Unit Test Suite ---\n\n");
    }

    // Suppress mirror of ERROR logs to stderr during testing
    logger_set_stderr(0);

    RUN_TEST(test_fs_util);
    RUN_TEST(test_config_store);
    RUN_TEST(test_logger);
    RUN_TEST(test_cache_store);
    RUN_TEST(test_platform);
    RUN_TEST(test_tl_serial);
    RUN_TEST(run_tl_skip_tests);
    RUN_TEST(test_ige);
    RUN_TEST(test_mtproto_crypto);
    RUN_TEST(test_phase2);
    RUN_TEST(test_rpc);
    RUN_TEST(test_auth);
    RUN_TEST(test_gzip);
    RUN_TEST(test_registry);
    RUN_TEST(test_api_call);
    RUN_TEST(run_auth_session_tests);
    RUN_TEST(run_auth_2fa_tests);
    RUN_TEST(run_auth_logout_tests);
    RUN_TEST(run_arg_parse_tests);
    RUN_TEST(run_watch_json_tests);
    RUN_TEST(run_readline_tests);
    RUN_TEST(run_domain_self_tests);
    RUN_TEST(run_domain_dialogs_tests);
    RUN_TEST(run_domain_history_tests);
    RUN_TEST(run_domain_updates_tests);
    RUN_TEST(run_domain_user_info_tests);
    RUN_TEST(run_domain_search_tests);
    RUN_TEST(run_domain_contacts_tests);
    RUN_TEST(run_domain_media_tests);
    RUN_TEST(run_domain_send_tests);
    RUN_TEST(run_domain_read_history_tests);
    RUN_TEST(run_domain_edit_delete_forward_tests);
    RUN_TEST(run_domain_upload_tests);
    RUN_TEST(run_dc_config_tests);
    RUN_TEST(run_session_store_tests);
    RUN_TEST(run_dc_session_tests);
    RUN_TEST(run_auth_transfer_tests);
    RUN_TEST(run_updates_state_store_tests);
    RUN_TEST(run_media_index_tests);
    RUN_TEST(test_tui_screen_run);
    RUN_TEST(test_tui_pane_run);
    RUN_TEST(test_tui_list_view_run);
    RUN_TEST(test_tui_dialog_pane_run);
    RUN_TEST(test_tui_history_pane_run);
    RUN_TEST(test_tui_status_row_run);
    RUN_TEST(test_tui_app_run);

    printf("\n--- Test Results ---\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_run - g_tests_failed);
    printf("Tests Failed: %d\n", g_tests_failed);

    if (g_tests_failed > 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
