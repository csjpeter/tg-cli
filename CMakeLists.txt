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

# Main executable
add_executable(${PROJECT_NAME}
    src/main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE tg-proto)

# Unit tests
enable_testing()
add_subdirectory(tests/unit)
