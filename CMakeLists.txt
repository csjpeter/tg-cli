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
    )
else()
    set(PLATFORM_SOURCES
        src/platform/posix/terminal.c
        src/platform/posix/path.c
    )
endif()

# Main executable
add_executable(${PROJECT_NAME}
    src/main.c
    src/core/config.c
    src/core/logger.c
    src/core/fs_util.c
    src/infrastructure/config_store.c
    src/infrastructure/cache_store.c
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/core
    src/infrastructure
    src/platform
)

target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# Unit tests
enable_testing()
add_subdirectory(tests/unit)
