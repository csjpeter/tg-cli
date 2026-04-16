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
