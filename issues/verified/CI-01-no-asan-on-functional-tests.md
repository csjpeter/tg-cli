# CI-01 — Functional tests not run with ASAN (and ASAN not wired in CMake at all)

## Category
CI / Build

## Severity
High

## Finding
`manage.sh` and `CLAUDE.md` both claim `./manage.sh test` runs "with ASAN", but the
CMake build system never enables `-fsanitize=address,undefined`.  The Debug build type
only adds `-g` (confirmed via `CMakeCache.txt`: `CMAKE_C_FLAGS_DEBUG:STRING=-g`).

Additionally, the functional test runner (`functional-test-runner`) is built via
`build_functional_runner()` which calls `cmake --build` without any configure step —
it reuses whatever the last configuration was.  Even if ASAN were wired up for unit
tests it would need a dedicated configure step for functional tests.

The CI workflow (`ci.yml`) runs only unit tests with ASAN (`./manage.sh test`) and
never runs the functional suite at all.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/manage.sh:22` — "test … Build and run unit tests (with ASAN)"
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt` — no `fsanitize` flag anywhere
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/ci.yml` — no `functional-test-runner` step

## Fix
1. Add to `CMakeLists.txt` under `if(CMAKE_BUILD_TYPE STREQUAL "Debug")`:
   ```cmake
   add_compile_options(-fsanitize=address,undefined)
   add_link_options(-fsanitize=address,undefined)
   ```
2. Add a `./manage.sh functional-test` command that runs `build_debug` + `build_functional_runner`
   + executes the functional runner.
3. Add a CI step to run functional tests with ASAN on every PR.
