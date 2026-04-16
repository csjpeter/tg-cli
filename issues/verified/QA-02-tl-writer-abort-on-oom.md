# tl_writer_grow() calls abort() on OOM

## Description
`tl_writer_grow()` in `src/core/tl_serial.c:23` calls bare `abort()` when `realloc`
fails, with no diagnostic output. Add a stderr/log message before aborting so that
OOM is diagnosable.

## Steps
1. Add `fprintf(stderr, "OOM: tl_writer_grow realloc failed\n");` before `abort()`

## OOM Policy
No graceful recovery needed. Abort is fine, just add a diagnostic message.

## Estimate
~2 lines

## Dependencies
None

## Reviewed — 2026-04-16
Pass. Confirmed tl_serial.c writer_ensure (line 19-30) prints `fprintf(stderr, "OOM: tl_writer realloc failed (%zu bytes)", new_cap)` before abort() on realloc failure.

## QA — 2026-04-16
Pass. tl_serial.c line 25 emits `OOM: tl_writer realloc failed (%zu bytes)` to stderr before abort(). Diagnostic message makes OOM post-mortem possible. No test regression.
