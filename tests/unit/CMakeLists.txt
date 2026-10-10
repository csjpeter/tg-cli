include_directories(
    ../common
    ../mocks
)

set(TEST_SOURCES
    test_runner.c
    test_platform.c
    test_fs_util.c
    test_config.c
    test_logger.c
    test_cache.c
    test_tl_serial.c
    test_tl_skip.c
    test_ige_aes.c
    test_mtproto_crypto.c
    test_phase2.c
    test_rpc.c
    test_transport.c
    test_auth.c
    test_gzip.c
    test_registry.c
    test_api_call.c
    test_auth_session.c
    test_pii_redact.c
    test_auth_2fa.c
    test_auth_logout.c
    test_arg_parse.c
    test_watch_json.c
    test_watch_sigpipe.c
    test_readline.c
    test_domain_self.c
    test_domain_dialogs.c
    test_domain_history.c
    test_domain_updates.c
    test_domain_user_info.c
    test_domain_search.c
    test_domain_contacts.c
    test_domain_media.c
    test_domain_send.c
    test_domain_read_history.c
    test_domain_edit_delete_forward.c
    test_domain_upload.c
    test_dc_config.c
    test_session_store.c
    test_dc_session.c
    test_auth_transfer.c
    test_updates_state_store.c
    test_media_index.c
    test_tui_screen.c
    test_tui_pane.c
    test_tui_list_view.c
    test_tui_dialog_pane.c
    test_tui_history_pane.c
    test_tui_status_row.c
    test_tui_app.c
    test_tg_tui_argv.c
    ../../tests/mocks/crypto.c
    ../../tests/mocks/socket.c
    ../../tests/mocks/telegram_server_key.c
)

add_executable(test-runner ${TEST_SOURCES})
target_link_libraries(test-runner PRIVATE tg-domain-write tg-domain-read tg-app tg-tui-lib tg-proto OpenSSL::SSL OpenSSL::Crypto)


# terminal_raw_enter() failure paths (TG-4): terminal.c is compiled again with
# tcgetattr/tcsetattr renamed to the fakes in the test.  No other test links
# this copy.
add_executable(test-terminal-raw-faults
    test_terminal_raw_faults.c
    ${CMAKE_SOURCE_DIR}/src/platform/posix/terminal.c
    ${CMAKE_SOURCE_DIR}/src/core/wcwidth.c)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/platform/posix/terminal.c
    TARGET_DIRECTORY test-terminal-raw-faults
    PROPERTIES COMPILE_DEFINITIONS "tcgetattr=tg_test_tcgetattr;tcsetattr=tg_test_tcsetattr;poll=tg_test_poll;isatty=tg_test_isatty")
target_include_directories(test-terminal-raw-faults PRIVATE ${CMAKE_SOURCE_DIR}/src)
