# tests/integration/CMakeLists.txt
#
# Builds the `tg-integration-test-runner` executable.
#
# Key differences from the functional-test-runner:
#  - Links production socket + crypto (NOT tests/mocks/).
#  - Reads TG_TEST_* env vars at runtime to reach the real Telegram test DC.
#  - Target is EXCLUDED_FROM_ALL — not built by default; built explicitly via
#    `cmake --build <dir> --target tg-integration-test-runner` or
#    `./manage.sh integration`.
#
# This target intentionally does NOT link tests/mocks/socket.c or
# tests/mocks/crypto.c so that tests exercise the full production code path.

include_directories(
    ../common          # test_helpers.h
)

set(INTEGRATION_SOURCES
    test_runner.c
    test_integration_smoke.c
)

add_executable(tg-integration-test-runner EXCLUDE_FROM_ALL ${INTEGRATION_SOURCES})

# Link production libraries only — no mock socket, no mock crypto.
# tg-app already pulls in tg-proto + tg-crypto (real OpenSSL) + platform socket.
target_link_libraries(tg-integration-test-runner PRIVATE
    tg-domain-write
    tg-domain-read
    tg-app
    tg-proto
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(tg-integration-test-runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
