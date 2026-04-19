# DOC-15: logging.md describes LOG_DEBUG MTProto traffic capture that is not implemented

## Location
`docs/dev/logging.md`, section "MTProto Traffic Capture"
`src/infrastructure/mtproto_rpc.c`, `src/infrastructure/api_call.c`

## Problem
`logging.md` states:

> At `LOG_DEBUG` the RPC layer emits, per frame:
> - direction (→ server / ← server),
> - outer 4-byte abridged-transport length prefix,
> - `auth_key_id` (8 bytes), `msg_key` (16 bytes), ciphertext length,
> - decrypted inner envelope: `salt`, `session_id`, `msg_id`, `seq_no`,
>   `length`, TL constructor CRC32.

There are **zero** `logger_log(LOG_DEBUG, ...)` calls in `mtproto_rpc.c` or
`api_call.c`.  The feature does not exist; the documentation describes a future
intent or a deleted implementation.

This is a second case of documentation/reality divergence alongside DOC-13
(`TG_CLI_LOG_PLAINTEXT` unimplemented).

## Fix direction
Either implement the LOG_DEBUG trace lines (preferred — they are valuable for
debugging real sessions), or update `logging.md` to reflect the actual current
state and add a FEAT ticket for the implementation.

If implementing: note that `auth_key` bytes must **not** appear in the trace;
only `auth_key_id` (which is safe to log) and `msg_key` are acceptable.
