# CI-02 — No static analysis (clang-tidy / cppcheck / scan-build) in CI

## Category
CI

## Severity
Medium

## Finding
The three CI workflows (`ci.yml`, `coverage.yml`, `valgrind.yml`) perform no
static analysis pass.  There is no `clang-tidy`, `cppcheck`, or `scan-build`
step anywhere.  With `-Wall -Wextra -Werror -pedantic` set in `CMakeLists.txt`
compiler diagnostics are enforced, but these do not catch use-after-free,
null-dereference patterns, or API misuse that a dedicated static analyser would
surface.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/ci.yml` — three steps only: build, unit-test, valgrind
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt:9` — `add_compile_options(-Wall -Wextra -Werror -pedantic)`
- No `clang-tidy`, `cppcheck`, or `scan-build` invocation in any `.github/workflows/*.yml`

## Fix
Add a CI job that runs `scan-build ./manage.sh build` (trivial, requires only
`clang-tools`) or add a `clang-tidy` CMake integration using
`CMAKE_C_CLANG_TIDY`.  A minimal `.clang-tidy` config checking
`clang-analyzer-*` and `cert-*` suffices as a first pass.
