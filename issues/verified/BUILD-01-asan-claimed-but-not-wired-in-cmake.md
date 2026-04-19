# BUILD-01 — ASAN claimed in manage.sh/CLAUDE.md but not configured in CMake

## Category
Build

## Severity
High

## Finding
`manage.sh` help text, `CLAUDE.md`, and the README all describe `./manage.sh debug`
and `./manage.sh test` as building "with ASAN".  The CMake `Debug` build type
however only adds `-g`; no `-fsanitize=address,undefined` flags are present in
`CMakeLists.txt` or any CMake preset file.

Verified by inspecting the generated cache:
```
CMAKE_C_FLAGS_DEBUG:STRING=-g
```

This means:
- Memory bugs, use-after-free, stack overflows, and UB go undetected.
- The documentation is actively misleading developers into trusting a safety net
  that does not exist.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt` — no `fsanitize` mention
- `cmake -DCMAKE_BUILD_TYPE=Debug` → `CMAKE_C_FLAGS_DEBUG:STRING=-g` (confirmed in session)
- `/home/csjpeter/ai-projects/tg-cli/manage.sh:20,22` — "Debug mode (with ASAN)", "unit tests (with ASAN)"

## Fix
In `CMakeLists.txt`, add:

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
```

Note: ASAN and coverage (`--coverage`) are mutually exclusive; keep the
`ENABLE_COVERAGE` path as-is and only apply ASAN when coverage is OFF.
