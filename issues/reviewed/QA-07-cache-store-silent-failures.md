# cache_store: silent failures, no logging

## Description
`cache_save()` and `cache_load()` in `src/infrastructure/cache_store.c` fail
silently — no `logger_log()` calls on error paths. This makes debugging cache
issues unnecessarily difficult.

Also, cache test coverage is minimal (56 LOC for 126 LOC module):
- No test for `cache_path()` returning NULL
- No test for disk-full / write-failure scenarios
- No test for corrupted cache files

## Steps
1. Add `logger_log(LOG_ERROR, ...)` on failure paths in cache_save/cache_load
2. Add tests for error paths (NULL path, write failure)

## Estimate
~30 lines

## Dependencies
None
