add_library(ptytest STATIC pty_session.c pty_screen.c pty_sync.c)
target_include_directories(ptytest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Suppress the null-dereference false-positive in the inlined scroll_up/cell_at
# pattern. GCC cannot prove at the call-sites inside scroll_up that the row/col
# arguments are in-bounds, even though the loop invariant guarantees it.
target_compile_options(ptytest PRIVATE -Wno-null-dereference)

# Link against libutil on Linux (for forkpty/openpty)
if(NOT APPLE)
    target_link_libraries(ptytest PRIVATE util)
endif()
