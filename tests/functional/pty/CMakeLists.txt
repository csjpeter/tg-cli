if(NOT UNIX)
    return()
endif()

add_executable(pty-test-runner
    test_pty_smoke.c
)

target_link_libraries(pty-test-runner PRIVATE ptytest)

add_test(NAME PtyTests COMMAND pty-test-runner)
