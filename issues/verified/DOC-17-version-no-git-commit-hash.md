# DOC-17 — `tg-cli --version` prints bare "0.1.0" with no git commit hash

## Category
Documentation / Build

## Severity
Low

## Finding
`arg_print_version()` in `src/core/arg_parse.c:474` prints:
```
tg-cli 0.1.0
```
The version string `TG_CLI_VERSION` is hardcoded at `src/core/arg_parse.c:12`.
CMake never injects a git commit hash or build date.

For a pre-release project with no published tarballs, `0.1.0` alone is
unhelpful for bug reports.  Users (and CI logs) cannot trace a binary to a
specific commit.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/core/arg_parse.c:12` — `#define TG_CLI_VERSION "0.1.0"`
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt` — no `git describe` or `GIT_COMMIT` definition

## Fix
In `CMakeLists.txt`, add:
```cmake
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty
        OUTPUT_VARIABLE GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
else()
    set(GIT_COMMIT "unknown")
endif()
target_compile_definitions(tg-proto PUBLIC TG_CLI_GIT_COMMIT="${GIT_COMMIT}")
```
Then update `arg_print_version()` to print `"tg-cli 0.1.0 (${GIT_COMMIT})"`.
