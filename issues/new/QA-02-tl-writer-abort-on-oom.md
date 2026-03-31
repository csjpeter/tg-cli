# tl_writer_grow() calls abort() on OOM

## Description
`tl_writer_grow()` in `src/core/tl_serial.c:23` calls `abort()` when `realloc` fails.
For a CLI tool this is overly harsh — error propagation (return -1) would allow
callers to handle the failure gracefully.

## Steps
1. Change `tl_writer_grow` to return int (0 success, -1 failure)
2. Update all `tl_write_*` functions to check and propagate the return value
3. Update callers to handle write failures
4. Add test for allocation failure path

## Estimate
~50 lines (many small changes across tl_serial.c)

## Dependencies
None
