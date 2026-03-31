# mtproto_compute_msg_key() fails silently on malloc error

## Description
`mtproto_compute_msg_key()` in `src/core/mtproto_crypto.c` (lines 49-78)
returns `void`. On malloc failure it silently returns, leaving the caller's
`msg_key` buffer uninitialized. Callers have no way to detect this error
and will proceed with garbage data.

## Steps
1. Change return type to `int` (0 = success, -1 = failure)
2. Update all callers to check return value
3. Add test for error propagation

## Estimate
~15 lines

## Dependencies
P1-mtproto-crypto (will be fixed together with that issue)
