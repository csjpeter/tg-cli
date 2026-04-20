include_directories(
    ../common
    ../mocks
)

set(FUNCTIONAL_SOURCES
    test_runner.c
    test_ige_aes_functional.c
    test_mtproto_crypto_functional.c
    test_crypto_kdf_functional.c
    test_srp_math_functional.c
    test_srp_roundtrip_functional.c
    test_tl_skip_message_functional.c
    test_tl_forward_compat.c
    test_mt_server_smoke.c
    test_login_flow.c
    test_read_path.c
    test_write_path.c
    test_upload_download.c
    test_dialogs_cache_ttl.c
    test_tui_e2e.c
    test_send_stdin.c
    test_dc_session_cache_skip.c
    test_logout_rpc.c
    test_wrong_session_id.c
    test_config_wizard_batch.c
    test_tg_cli_read_dispatch.c
    test_logger_lifecycle.c
    test_session_corruption.c
    test_config_ini_robustness.c
    test_resolver_cache.c
    test_text_rendering_safety.c
    # Real crypto implementation (not the mock used by unit tests).
    ../../src/core/crypto.c
    # Mock socket + in-process Telegram server emulator. Real crypto on
    # both sides — the only thing faked is the TCP transport.
    ../../tests/mocks/socket.c
    ../../tests/mocks/mock_tel_server.c
)

add_executable(functional-test-runner ${FUNCTIONAL_SOURCES})

# tg-app pulls in session_store_save/load; tg-proto brings in rpc +
# transport + crypto wrappers + SRP + auth_2fa. OpenSSL stays linked.
# tg-tui-lib adds dialog_pane/history_pane/screen/app for TEST-11.
target_link_libraries(functional-test-runner PRIVATE
    tg-domain-write tg-domain-read tg-app tg-proto tg-tui-lib
    OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME FunctionalTests COMMAND functional-test-runner)

# PTY-backed functional tests (POSIX only)
if(UNIX)
    add_subdirectory(pty)
endif()
