# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-03-29
# CMake build rules for the project.

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

# libFuzzer support (declared early so tg-proto is instrumented too)
# The actual subdirectory is added at the bottom; the option is here so the
# global -fsanitize=fuzzer-no-link flag propagates to every static library.
option(ENABLE_FUZZ "Build libFuzzer harness (requires clang)" OFF)
if(ENABLE_FUZZ)
    # -fsanitize=fuzzer-no-link: instrument all TUs for coverage feedback
    # without pulling in the fuzzer main() into every library archive.
    # The fuzz executable target adds -fsanitize=fuzzer (which implies the
    # full driver) in tests/fuzz/CMakeLists.txt.
    add_compile_options(-g -O1 -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
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

# Find pthreads (used by the PTY test stub server)
find_package(Threads REQUIRED)

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
    src/core/pii_redact.c
    # Production Telegram RSA key; functional-test-runner overrides by
    # compiling tests/mocks/telegram_server_key.c first in the link order.
    src/core/telegram_server_key.c
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
if(UNIX)
    target_link_libraries(tg-proto PUBLIC tuikit::tuikit_static)
endif()

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
    src/app/config_wizard.c
    src/app/credentials.c
    src/app/session_store.c
    src/app/dc_session.c
)
target_include_directories(tg-app PUBLIC src src/app)
target_link_libraries(tg-app PUBLIC tg-proto tg-crypto)

# ---- TUI: pane-based curses-style layer (US-11 v2) ----
# Only linked by tg-tui. Other binaries are batch and do not need it.

add_library(tg-tui-lib STATIC
    src/tui/pane.c
    src/tui/dialog_pane.c
    src/tui/history_pane.c
    src/tui/status_row.c
    src/tui/text_safe.c
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

# Claude skill for the CLI (policies/cli-skill-model.md): installed by the
# package under /usr/share/claude-skills/<name>/, never into a home directory.
install(FILES claude/skills/tg-cli/SKILL.md
        DESTINATION share/claude-skills/tg-cli)

# The skill must agree with the man pages.
add_test(NAME SkillMatchesManPages
         COMMAND ${CMAKE_SOURCE_DIR}/tests/skill/check_skill.sh ${CMAKE_SOURCE_DIR})

# The workspace's own libraries (policies/dependencies-model.md): a pinned git
# submodule under deps/ built and linked statically ("bundled"), or the
# installed -dev package ("system").  tg-cli is proprietary, so "auto" prefers
# the installed package and falls back to the submodule.
set(WORKSPACE_LIBS "auto" CACHE STRING
    "How libtuikit and libptytest are obtained: bundled (deps/ submodule), system (-dev package) or auto")
set_property(CACHE WORKSPACE_LIBS PROPERTY STRINGS auto system bundled)
if(NOT WORKSPACE_LIBS MATCHES "^(auto|system|bundled)$")
    message(FATAL_ERROR "WORKSPACE_LIBS must be bundled, system or auto, not '${WORKSPACE_LIBS}'")
endif()

# workspace_lib(<name> <package> <purpose> [<cache-option> <value>]...)
# Provides the CMake package <package>'s <package>::<package>_static target
# either from the installed -dev package or from deps/<name>.
function(workspace_lib name package purpose)
    set(how "")
    if(NOT WORKSPACE_LIBS STREQUAL "bundled")
        find_package(${package} QUIET)
        if(${package}_FOUND)
            set(how "system (installed ${name}-dev ${${package}_VERSION})")
        elseif(WORKSPACE_LIBS STREQUAL "system")
            message(FATAL_ERROR "WORKSPACE_LIBS=system but ${name}-dev is not installed. "
                "Install ${name}-dev, or use WORKSPACE_LIBS=bundled with the deps/${name} submodule.")
        endif()
    endif()
    if(NOT how)
        if(EXISTS "${CMAKE_SOURCE_DIR}/deps/${name}/CMakeLists.txt")
            string(TOUPPER ${package} up)
            set(${up}_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            add_subdirectory(deps/${name} EXCLUDE_FROM_ALL)
            set(how "bundled (deps/${name} submodule)")
        else()
            message(FATAL_ERROR "${name} is needed for ${purpose} and neither "
                "way is available: install ${name}-dev, or run "
                "'git submodule update --init --recursive' for deps/${name} "
                "(./manage.sh deps does it).")
        endif()
    endif()
    message(STATUS "${name}: ${how}")
endfunction()

# The terminal toolkit: the TUI, the line editor and the password prompt are
# built on it, linked statically as tuikit::tuikit_static.
if(UNIX)
    workspace_lib(libtuikit tuikit "the TUI and the terminal input")
    workspace_lib(libptytest ptytest "the PTY tests")
endif()

# Unit tests
enable_testing()
add_subdirectory(tests/unit)

# Functional tests (real crypto, no mocks)
add_subdirectory(tests/functional)

# Integration tests — target tg-integration-test-runner, EXCLUDED_FROM_ALL.
# Build explicitly with: cmake --build <dir> --target tg-integration-test-runner
# or via: ./manage.sh integration
add_subdirectory(tests/integration)

# libFuzzer harness — only built when -DENABLE_FUZZ=ON is passed.
# The option is declared earlier (near ENABLE_COVERAGE) so the
# -fsanitize=fuzzer-no-link flag is applied to all static libraries.
if(ENABLE_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
