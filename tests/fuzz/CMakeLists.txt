# tests/fuzz/CMakeLists.txt
#
# libFuzzer harness for the TL binary parser.
#
# This sub-directory is only added to the build when -DENABLE_FUZZ=ON is
# passed to CMake.  The target requires clang (not GCC) because libFuzzer
# is a clang-specific sanitizer.  The normal GCC Release/Debug/Test builds
# are completely unaffected.
#
# Usage:
#   cmake -DENABLE_FUZZ=ON -DCMAKE_C_COMPILER=clang [build-dir] ..
#   cmake --build [build-dir] --target fuzz-tl-parse
#
# Or use the convenience wrapper:
#   ./manage.sh fuzz

# Enforce clang — libFuzzer is not available under GCC.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "ENABLE_FUZZ=ON requires clang (CMAKE_C_COMPILER=clang). "
        "Current compiler: ${CMAKE_C_COMPILER_ID}")
endif()

add_executable(fuzz-tl-parse fuzz_tl_parse.c)

# Link the minimal set of production libraries needed by the fuzz target.
# We intentionally avoid tg-domain-* and tg-app so the harness stays fast
# and we are not dragged into OpenSSL / socket dependencies.
# tg-proto contains tl_serial.c + tl_skip.c.
target_link_libraries(fuzz-tl-parse PRIVATE tg-proto tg-crypto)

target_include_directories(fuzz-tl-parse PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

# The -fsanitize=fuzzer-no-link,address flags are applied globally (via the
# ENABLE_FUZZ block in the root CMakeLists.txt) so all static libraries are
# instrumented.  Here we only add the fuzzer driver itself (-fsanitize=fuzzer
# on the link step brings in the libFuzzer main()).
target_link_options(fuzz-tl-parse PRIVATE
    -fsanitize=fuzzer,address
)
