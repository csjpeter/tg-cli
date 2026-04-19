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
