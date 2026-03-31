# fclose() return value not checked in logger

## Description
`logger_close()` in `src/core/logger.c` (lines 69 and 110) ignores the return
value of `fclose()`. On network filesystems or full disks, `fclose()` can fail
and the last buffered writes may be lost silently.

## Steps
1. Check `fclose()` return value
2. Log or report failure (at minimum to stderr, since the logger itself is closing)

## Estimate
~5 lines

## Dependencies
None
