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

## Reviewed — 2026-04-16
Pass. Confirmed cache_store.c adds logger_log(LOG_ERROR,...) on mkdir_p failure (line 46), fopen failure (line 52), fwrite short-write (line 57), cache_load NULL path (line 84), cache_load missing data (line 90, DEBUG). Tests added for load-missing and save-mkdir-fails (test_cache.c lines 60-95).
