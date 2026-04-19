# TEST-35: No test for session_store partial-write / truncation recovery

## Location
`src/app/session_store.c` – `write_file()` / `read_file()`
`tests/unit/test_session_store.c`

## Problem
`write_file` truncates `session.bin` via `fopen(path, "wb")` and then writes
the new content.  If the process crashes (or disk fills) after truncation but
before the `fwrite`, the file is left as zero bytes.  `read_file()` returns -1
on `n < STORE_HEADER`, so the next load treats it as a corrupt file and starts
fresh (silent deauth).

There are no tests for this scenario:
1. A pre-existing valid session file is truncated to 0 bytes; `session_store_load`
   should return -1 (not crash or return stale data).
2. A file truncated after the header (magic + version present, body missing);
   `read_file` should log `"truncated body"` and return -1.
3. A file with a valid header but `count` = `SESSION_STORE_MAX_DCS + 1` →
   should return -1 via the bounds check.

`TEST-26` (version mismatch) is the only negative path tested for `session_store`.

## Add to
`tests/unit/test_session_store.c`
