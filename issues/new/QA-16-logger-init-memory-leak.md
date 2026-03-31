# logger_init() leaks memory and file handle on repeated calls

## Description
`logger_init()` in `src/core/logger.c` (line 61) calls `strdup(log_file_path)`
and assigns to `g_log_path` without freeing any previous value. Similarly,
`g_log_fp` is overwritten without closing the previous file handle.

If `logger_init()` is called more than once (e.g., after config reload or
in tests), each call leaks the previous path string and file descriptor.

## Severity
HIGH — file descriptor leak (finite OS resource) + memory leak.

## Steps
1. At the top of `logger_init()`, free `g_log_path` and fclose `g_log_fp` if non-NULL
2. Add unit test: call `logger_init()` twice, verify no Valgrind leak

## Estimate
~8 lines

## Dependencies
None
