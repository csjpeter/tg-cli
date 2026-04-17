include_directories(
    ../common
)

set(FUNCTIONAL_SOURCES
    test_runner.c
    test_ige_aes_functional.c
    test_mtproto_crypto_functional.c
    test_crypto_kdf_functional.c
    test_srp_math_functional.c
    test_srp_roundtrip_functional.c
    test_tl_skip_message_functional.c
    # Real crypto implementation (not the mock used by unit tests)
    ../../src/core/crypto.c
)

add_executable(functional-test-runner ${FUNCTIONAL_SOURCES})

# Link against tg-proto + real crypto (no mocks) + OpenSSL. We also
# need the full app/infra stack to pull in auth_2fa + the SRP helper.
target_link_libraries(functional-test-runner PRIVATE
    tg-proto OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME FunctionalTests COMMAND functional-test-runner)
