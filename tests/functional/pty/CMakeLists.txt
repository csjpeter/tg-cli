if(NOT UNIX)
    return()
endif()

# ── PTY smoke tests (no Telegram; exercises libptytest itself) ────────
add_executable(pty-test-runner
    test_pty_smoke.c
)

target_link_libraries(pty-test-runner PRIVATE ptytest)

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
    ${CMAKE_SOURCE_DIR}/libs/libptytest
)

target_compile_definitions(pty-tui-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-tui-test-runner PRIVATE
    ptytest tg-app tg-proto
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
    ${CMAKE_SOURCE_DIR}/libs/libptytest
)

target_compile_definitions(pty-tui-resize-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-tui-resize-test-runner PRIVATE
    ptytest tg-app tg-proto
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME PtyTuiResizeTests COMMAND pty-tui-resize-test-runner)

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
    ${CMAKE_SOURCE_DIR}/libs/libptytest
)

target_compile_definitions(pty-readline-test-runner PRIVATE
    RL_HARNESS_BINARY="$<TARGET_FILE:rl-harness>"
)

target_link_libraries(pty-readline-test-runner PRIVATE ptytest)

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
    ${CMAKE_SOURCE_DIR}/libs/libptytest
)

target_compile_definitions(pty-ctrl-c-test-runner PRIVATE
    TG_TUI_BINARY="${CMAKE_BINARY_DIR}/tg-tui"
)

target_link_libraries(pty-ctrl-c-test-runner PRIVATE
    ptytest tg-app tg-proto
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
    ${CMAKE_SOURCE_DIR}/libs/libptytest
)

target_compile_definitions(pty-wizard-test-runner PRIVATE
    TG_CLI_BINARY="${CMAKE_BINARY_DIR}/tg-cli"
)

target_link_libraries(pty-wizard-test-runner PRIVATE ptytest)

add_test(NAME PtyWizardTests COMMAND pty-wizard-test-runner)
