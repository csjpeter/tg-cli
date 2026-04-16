cmake_minimum_required(VERSION 3.16)
project(tg-cli C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_definitions(_GNU_SOURCE)
add_compile_options(-Wall -Wextra -Werror -pedantic)

# Coverage support
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -g)
    add_link_options(--coverage)
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES
        src/platform/windows/terminal.c
        src/platform/windows/path.c
        src/platform/windows/socket.c
    )
else()
    set(PLATFORM_SOURCES
        src/platform/posix/terminal.c
        src/platform/posix/path.c
        src/platform/posix/socket.c
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
    src/core/config.c
    src/core/logger.c
    src/core/fs_util.c
    src/core/tl_serial.c
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
)
target_include_directories(tg-app PUBLIC src src/app)
target_link_libraries(tg-app PUBLIC tg-proto tg-crypto)

# ---- Domain: read-only module (tg-cli-ro, tg-tui, tg-cli all link this) ----
# Sources will grow as US-04, US-05, US-06, US-07 land.

add_library(tg-domain-read STATIC
    src/domain/read/self.c
    src/domain/read/dialogs.c
    src/domain/read/history.c
    src/domain/read/updates.c
    src/domain/read/user_info.c
    src/domain/read/search.c
)
target_include_directories(tg-domain-read PUBLIC src src/domain)
target_link_libraries(tg-domain-read PUBLIC tg-app)

# ---- Binaries ----

add_executable(tg-cli-ro src/main/tg_cli_ro.c)
target_link_libraries(tg-cli-ro PRIVATE tg-domain-read)

add_executable(tg-tui src/main/tg_tui.c)
target_link_libraries(tg-tui PRIVATE tg-domain-read)

# Unit tests
enable_testing()
add_subdirectory(tests/unit)

# Functional tests (real crypto, no mocks)
add_subdirectory(tests/functional)
