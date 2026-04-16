# mtproto_compute_msg_key() fails silently on malloc error

## Description
`mtproto_compute_msg_key()` in `src/core/mtproto_crypto.c` (lines 49-78)
returns `void`. On malloc failure it silently returns, leaving the caller's
`msg_key` buffer uninitialized. Callers have no way to detect this error
and will proceed with garbage data.

## Steps
1. Add `fprintf(stderr, "OOM\n"); abort();` on malloc failure instead of silent return

## OOM Policy
No graceful recovery needed. Abort with message is sufficient.

## Estimate
~2 lines

## Dependencies
P1-mtproto-crypto (will be fixed together with that issue)

## Reviewed — 2026-04-16
Pass. Confirmed mtproto_crypto.c lines 96-99: `if (!buf) { fprintf(stderr, "OOM: mtproto_compute_msg_key"); abort(); }`. Same pattern in mtproto_encrypt lines 160-163. No silent returns on malloc failure.
