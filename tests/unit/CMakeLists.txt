include_directories(
    ../../src
    ../../src/core
    ../../src/infrastructure
    ../../src/platform
    ../../src/vendor/tinf
    ../common
    ../mocks
)

# Platform-specific sources for tests
if(WIN32)
    set(TEST_PLATFORM_SOURCES
        ../../src/platform/windows/terminal.c
        ../../src/platform/windows/path.c
    )
else()
    set(TEST_PLATFORM_SOURCES
        ../../src/platform/posix/terminal.c
        ../../src/platform/posix/path.c
    )
endif()

set(TEST_SOURCES
    test_runner.c
    test_platform.c
    test_fs_util.c
    test_config.c
    test_logger.c
    test_cache.c
    test_tl_serial.c
    test_ige_aes.c
    test_mtproto_crypto.c
    test_phase2.c
    test_rpc.c
    test_auth.c
    test_gzip.c
    test_registry.c
    test_api_call.c
    ../../src/core/fs_util.c
    ../../src/core/logger.c
    ../../src/core/config.c
    ../../src/core/tl_serial.c
    ../../src/core/ige_aes.c
    ../../src/core/mtproto_crypto.c
    ../../src/core/mtproto_session.c
    ../../src/core/mtproto_auth.c
    ../../src/core/tl_registry.c
    ../../src/infrastructure/config_store.c
    ../../src/infrastructure/cache_store.c
    ../../src/infrastructure/transport.c
    ../../src/infrastructure/mtproto_rpc.c
    ../../src/infrastructure/api_call.c
    ../../tests/mocks/crypto.c
    ../../tests/mocks/socket.c
    ../../src/vendor/tinf/tinflate.c
    ../../src/vendor/tinf/tinfgzip.c
    ../../src/vendor/tinf/tinfzlib.c
    ../../src/vendor/tinf/adler32.c
    ../../src/vendor/tinf/crc32.c
    ${TEST_PLATFORM_SOURCES}
)

add_executable(test-runner ${TEST_SOURCES})
target_link_libraries(test-runner OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME UnitTests COMMAND test-runner)
