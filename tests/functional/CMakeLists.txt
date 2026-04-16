include_directories(
    ../common
)

set(FUNCTIONAL_SOURCES
    test_runner.c
    test_ige_aes_functional.c
    test_mtproto_crypto_functional.c
    test_crypto_kdf_functional.c
    # Real crypto implementation (not the mock used by unit tests)
    ../../src/core/crypto.c
)

add_executable(functional-test-runner ${FUNCTIONAL_SOURCES})

# Link against tg-proto + real crypto (no mocks) + OpenSSL
target_link_libraries(functional-test-runner PRIVATE tg-proto OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME FunctionalTests COMMAND functional-test-runner)
