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
    test_mt_server_smoke.c
    test_login_flow.c
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
target_link_libraries(functional-test-runner PRIVATE
    tg-app tg-proto OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME FunctionalTests COMMAND functional-test-runner)
