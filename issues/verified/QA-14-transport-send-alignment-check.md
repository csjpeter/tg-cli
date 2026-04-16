# transport_send does not validate 4-byte alignment

## Description
`transport_send()` in `src/infrastructure/transport.c` (line 48) computes
`wire_len = len / 4` using integer division. If `len` is not a multiple of 4,
the division silently truncates, and the length prefix sent to the server
does not match the actual payload size.

The MTProto Abridged transport requires all payloads to be 4-byte aligned.
Sending a misaligned payload causes protocol desynchronization — the server
reads the wrong number of bytes and the TCP stream becomes unrecoverable.

## Severity
HIGH — protocol violation causes silent data corruption on the wire.

## Steps
1. Add check: `if (len % 4 != 0) return -1;`
2. Add unit test: call `transport_send()` with `len = 5, 7, 13` and verify -1 return

## Estimate
~5 lines code + ~15 lines tests

## Dependencies
None

## Verified — 2026-04-16
- Added `if (len % 4 != 0) return -1;` with error log to `transport_send` before length-prefix encoding.
- Regression test `test_transport_send_unaligned_len` verifies -1 return for len=5,7,13.
- Unit test count: 1816 → 1819 (ASAN clean). Valgrind: 0 bytes definitely lost.
