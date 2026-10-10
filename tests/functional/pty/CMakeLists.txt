# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-04-19
# CMake build rules for tests/functional/pty.

if(NOT UNIX)
    return()
endif()

# ── PTY smoke tests (no Telegram; exercises libptytest itself) ────────
add_executable(pty-test-runner
    test_pty_smoke.c
)

target_link_libraries(pty-test-runner PRIVATE ptytest::ptytest_static)

add_test(NAME PtyTests COMMAND pty-test-runner)

# ── PTY TUI startup tests (PTY-02) ───────────────────────────────────
# These require the full tg-app / tg-proto / tg-crypto stack so the stub
# server can encrypt / decrypt real MTProto frames and seed session.bin.
add_executable(pty-tui-test-runner
    test_tui_startup.c
    pty_tel_stub.c
    # Real crypto (not mock) — the stub must speak real AES-IGE.
    ../../../src/core/crypto.c
)

target_include_directories(pty-tui-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_SOURCE_DIR}/tests/mocks
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
)

target_compile_definitions(pty-tui-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-tui-test-runner PRIVATE
    ptytest::ptytest_static tg-app tg-proto
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME PtyTuiTests COMMAND pty-tui-test-runner)

# ── PTY-04: TUI SIGWINCH resize tests ────────────────────────────────
add_executable(pty-tui-resize-test-runner
    test_tui_resize.c
    pty_tel_stub.c
    ../../../src/core/crypto.c
)

target_include_directories(pty-tui-resize-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_SOURCE_DIR}/tests/mocks
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
)

target_compile_definitions(pty-tui-resize-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-tui-resize-test-runner PRIVATE
    ptytest::ptytest_static tg-app tg-proto
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME PtyTuiResizeTests COMMAND pty-tui-resize-test-runner)

# ── TG-7: TUI interface policy (header bar, palette, keys, help) ─────
add_executable(pty-tui-interface-test-runner
    test_tui_interface.c
    pty_tel_stub.c
    ../../../src/core/crypto.c
)

target_include_directories(pty-tui-interface-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_SOURCE_DIR}/tests/mocks
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
)

# The test's HOME lives under the build tree, never under /tmp.
target_compile_definitions(pty-tui-interface-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
    TG_PTY_HOME_BASE="${CMAKE_BINARY_DIR}/pty-tui-interface"
)

target_link_libraries(pty-tui-interface-test-runner PRIVATE
    ptytest::ptytest_static tg-app tg-proto
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME PtyTuiInterfaceTests COMMAND pty-tui-interface-test-runner)

# ── PTY-03: readline navigation tests ────────────────────────────────
# rl_harness: tiny binary that calls rl_readline in a loop and prints
# "ACCEPTED:<line>" for each accepted line.  No mock server needed.
add_executable(rl-harness
    rl_harness.c
)

target_include_directories(rl-harness PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(rl-harness PRIVATE tg-proto)

# Test runner that drives rl-harness through the PTY.
add_executable(pty-readline-test-runner
    test_readline.c
)

target_include_directories(pty-readline-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(pty-readline-test-runner PRIVATE
    RL_HARNESS_BINARY="$<TARGET_FILE:rl-harness>"
)

target_link_libraries(pty-readline-test-runner PRIVATE ptytest::ptytest_static)

add_test(NAME PtyReadlineTests COMMAND pty-readline-test-runner)

# ── PTY-05: Ctrl-C exit + terminal-restore tests ─────────────────────
add_executable(pty-ctrl-c-test-runner
    test_ctrl_c_exit.c
    pty_tel_stub.c
    ../../../src/core/crypto.c
)

target_include_directories(pty-ctrl-c-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_SOURCE_DIR}/tests/mocks
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
)

target_compile_definitions(pty-ctrl-c-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-ctrl-c-test-runner PRIVATE
    ptytest::ptytest_static tg-app tg-proto
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME PtyCtrlCTests COMMAND pty-ctrl-c-test-runner)

# ── PTY-06: config wizard interactive tests (FEAT-37) ────────────────
add_executable(pty-wizard-test-runner
    test_config_wizard_pty.c
)

target_include_directories(pty-wizard-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(pty-wizard-test-runner PRIVATE
    TG_CLI_BINARY="${CMAKE_BINARY_DIR}/tg-cli"
)

target_link_libraries(pty-wizard-test-runner PRIVATE ptytest::ptytest_static)

add_test(NAME PtyWizardTests COMMAND pty-wizard-test-runner)

# ── TEST-87: tui_tty_read_password masking tests ────────────────────
# password_harness: tiny binary that calls tui_tty_read_password() and
# prints the result.  Isolates the masked-input behaviour from the full
# tg-tui login path (no mock server needed).
add_executable(password_harness
    password_harness.c
)

target_include_directories(password_harness PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/platform
)

target_link_libraries(password_harness PRIVATE tg-app tg-proto)

# Test runner that drives password_harness through the PTY.
add_executable(pty-password-test-runner
    test_password_prompt.c
)

target_include_directories(pty-password-test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/functional/pty
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(pty-password-test-runner PRIVATE
    PASSWORD_HARNESS_BINARY="$<TARGET_FILE:password_harness>"
)

target_link_libraries(pty-password-test-runner PRIVATE ptytest::ptytest_static)

add_test(NAME PtyPasswordTests COMMAND pty-password-test-runner)

