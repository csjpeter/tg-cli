# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-04-29
# CMake build rules for tests/integration.

# tests/integration/CMakeLists.txt
#
# Builds two targets:
#
#   tg-integration-test-runner  — full test suite (./manage.sh integration)
#   tg-test-login               — one-shot interactive login helper
#                                 (./manage.sh test-login)
#
# Both targets are EXCLUDED_FROM_ALL and link production code only
# (no mock socket, no mock crypto).

set(INTEG_COMMON
    integ_config.c
)

set(INTEGRATION_SOURCES
    test_runner.c
    test_integration_smoke.c
    test_integration_live.c
    ${INTEG_COMMON}
)

set(INTEG_LIBS
    tg-domain-write
    tg-domain-read
    tg-app
    tg-proto
    OpenSSL::SSL
    OpenSSL::Crypto
)

set(INTEG_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

# ---- Test runner ----

# Part of the default build: a declared test must have its file in
# build/tests/integration/bin/ (policies/testing-model.md).  Running it needs a
# real Telegram test DC and credentials: ./manage.sh integration.
add_executable(tg-integration-test-runner ${INTEGRATION_SOURCES})
target_link_libraries(tg-integration-test-runner PRIVATE ${INTEG_LIBS})
target_include_directories(tg-integration-test-runner PRIVATE ${INTEG_INCLUDES})

# ---- Login helper ----

add_executable(tg-test-login EXCLUDE_FROM_ALL
    test_login.c
    ${INTEG_COMMON}
)
target_link_libraries(tg-test-login PRIVATE ${INTEG_LIBS})
target_include_directories(tg-test-login PRIVATE ${INTEG_INCLUDES})
