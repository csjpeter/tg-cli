cmake_minimum_required(VERSION 3.16)
project(tg-cli C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_definitions(_GNU_SOURCE)
add_compile_options(
    -Wall -Wextra -Werror -pedantic
    -Wshadow -Wformat-security -Wstrict-prototypes -Wnull-dereference
)

# Coverage support
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -g)
    add_link_options(--coverage)
endif()

# ASAN + UBSAN for Debug builds on GCC/Clang (skip Windows cross-compile where
# MinGW ASAN support is unreliable, and skip when coverage is enabled because
# --coverage and -fsanitize=address are mutually exclusive).
if(CMAKE_BUILD_TYPE STREQUAL "Debug"
   AND NOT WIN32
   AND NOT ENABLE_COVERAGE
   AND (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang"))
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Capture git commit hash at configure time
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty
        OUTPUT_VARIABLE TG_CLI_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
if(NOT TG_CLI_GIT_COMMIT)
    set(TG_CLI_GIT_COMMIT "unknown")
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES
        src/platform/windows/terminal.c
        src/platform/windows/path.c
        src/platform/windows/socket.c
        src/platform/windows/argv_utf8.c
    )
else()
    set(PLATFORM_SOURCES
        src/platform/posix/terminal.c
        src/platform/posix/path.c
        src/platform/posix/socket.c
        src/platform/posix/argv_utf8.c
    )
endif()

# Vendored libraries
set(VENDOR_TINF_SOURCES
    src/vendor/tinf/tinflate.c
    src/vendor/tinf/tinfgzip.c
    src/vendor/tinf/tinfzlib.c
    src/vendor/tinf/adler32.c
    src/vendor/tinf/crc32.c
)

# Protocol static library (core + infrastructure + platform + vendor)
add_library(tg-proto STATIC
    src/core/arg_parse.c
    src/core/readline.c
    src/core/wcwidth.c
    src/core/json_util.c
    src/core/config.c
    src/core/logger.c
    src/core/fs_util.c
    src/core/tl_serial.c
    src/core/tl_skip.c
    src/core/ige_aes.c
    src/core/mtproto_crypto.c
    src/core/mtproto_session.c
    src/core/tl_registry.c
    src/infrastructure/config_store.c
    src/infrastructure/cache_store.c
    src/infrastructure/transport.c
    src/infrastructure/mtproto_auth.c
    src/infrastructure/mtproto_rpc.c
    src/infrastructure/api_call.c
    src/infrastructure/auth_session.c
    src/infrastructure/auth_2fa.c
    src/infrastructure/auth_logout.c
    src/infrastructure/auth_transfer.c
    src/infrastructure/updates_state_store.c
    src/infrastructure/media_index.c
    ${PLATFORM_SOURCES}
    ${VENDOR_TINF_SOURCES}
)

target_include_directories(tg-proto PUBLIC
    src
    src/core
    src/infrastructure
    src/platform
    src/vendor/tinf
)

target_compile_definitions(tg-proto PUBLIC TG_CLI_GIT_COMMIT="${TG_CLI_GIT_COMMIT}")
target_link_libraries(tg-proto PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# Real OpenSSL crypto implementation. Production binaries link this;
# unit tests link tests/mocks/crypto.c instead. Kept separate so tg-proto
# can be linked against either backend.
add_library(tg-crypto STATIC src/core/crypto.c)
target_include_directories(tg-crypto PUBLIC src src/core)
target_link_libraries(tg-crypto PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# ---- App runtime (shared by all binaries) ----

add_library(tg-app STATIC
    src/app/bootstrap.c
    src/app/dc_config.c
    src/app/auth_flow.c
    src/app/credentials.c
    src/app/session_store.c
    src/app/dc_session.c
)
target_include_directories(tg-app PUBLIC src src/app)
target_link_libraries(tg-app PUBLIC tg-proto tg-crypto)

# ---- TUI: pane-based curses-style layer (US-11 v2) ----
# Only linked by tg-tui. Other binaries are batch and do not need it.

add_library(tg-tui-lib STATIC
    src/tui/screen.c
    src/tui/pane.c
    src/tui/list_view.c
    src/tui/dialog_pane.c
    src/tui/history_pane.c
    src/tui/status_row.c
    src/tui/app.c
)
target_include_directories(tg-tui-lib PUBLIC src src/tui)
target_link_libraries(tg-tui-lib PUBLIC tg-domain-read)

# ---- Domain: read-only module (tg-cli-ro, tg-tui, tg-cli all link this) ----
# Sources will grow as US-04, US-05, US-06, US-07 land.

add_library(tg-domain-read STATIC
    src/domain/read/self.c
    src/domain/read/dialogs.c
    src/domain/read/history.c
    src/domain/read/updates.c
    src/domain/read/user_info.c
    src/domain/read/search.c
    src/domain/read/contacts.c
    src/domain/read/media.c
)
target_include_directories(tg-domain-read PUBLIC src src/domain)
target_link_libraries(tg-domain-read PUBLIC tg-app)

# ---- Domain: write module (tg-cli + tg-tui link this; tg-cli-ro must NOT) ----
# ADR-0005: read-only binary is guarded at link time by simply not listing
# this library in its target_link_libraries.
add_library(tg-domain-write STATIC
    src/domain/write/send.c
    src/domain/write/read_history.c
    src/domain/write/edit.c
    src/domain/write/delete.c
    src/domain/write/forward.c
    src/domain/write/upload.c
)
target_include_directories(tg-domain-write PUBLIC src src/domain)
target_link_libraries(tg-domain-write PUBLIC tg-app tg-domain-read)

# ---- Binaries ----

add_executable(tg-cli-ro src/main/tg_cli_ro.c)
target_link_libraries(tg-cli-ro PRIVATE tg-domain-read)

add_executable(tg-tui src/main/tg_tui.c)
target_link_libraries(tg-tui PRIVATE tg-domain-read tg-domain-write tg-tui-lib)

add_executable(tg-cli src/main/tg_cli.c)
target_link_libraries(tg-cli PRIVATE tg-domain-read tg-domain-write)

# PTY test library (POSIX only)
if(UNIX)
    add_subdirectory(libs/libptytest)
endif()

# Unit tests
enable_testing()
add_subdirectory(tests/unit)

# Functional tests (real crypto, no mocks)
add_subdirectory(tests/functional)
