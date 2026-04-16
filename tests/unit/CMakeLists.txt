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
    test_auth.c
    test_gzip.c
    test_registry.c
    test_api_call.c
    test_auth_session.c
    test_auth_2fa.c
    test_arg_parse.c
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
    test_dc_config.c
    test_session_store.c
    ../../tests/mocks/crypto.c
    ../../tests/mocks/socket.c
)

add_executable(test-runner ${TEST_SOURCES})
target_link_libraries(test-runner PRIVATE tg-domain-write tg-domain-read tg-app tg-proto OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME UnitTests COMMAND test-runner)
