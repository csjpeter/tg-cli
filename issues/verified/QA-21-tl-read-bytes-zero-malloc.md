# tl_read_bytes() calls malloc(0) for zero-length data

## Description
`tl_read_bytes()` in `src/core/tl_serial.c` (line 270) calls `malloc(data_len)`
when `data_len == 0`. Per C11 §7.22.3, `malloc(0)` is implementation-defined:
it may return NULL or a unique non-NULL pointer.

If it returns NULL, the caller interprets this as allocation failure and
returns an error — even though a zero-length byte string is a valid TL value.
This affects `tl_read_string()` (line 234) which calls `tl_read_bytes()` and
propagates the NULL.

## Severity
MEDIUM — valid zero-length TL strings/bytes fail on some platforms.

## Steps
1. Change to `malloc(data_len ? data_len : 1)` to guarantee non-NULL return
2. Add unit test: write a zero-length bytes field, read it back, verify success

## Estimate
~3 lines code + ~10 lines tests

## Dependencies
None

## Verified — 2026-04-16
- Changed `malloc(data_len)` to `malloc(data_len ? data_len : 1)` in `tl_read_bytes`.
- Added `test_tl_read_bytes_empty` regression test covering the zero-length round-trip.
- `./manage.sh test` passes: 1823 tests.
