# 65KB stack buffers in mtproto_rpc.c

## Description
Multiple functions in `src/infrastructure/mtproto_rpc.c` (lines 46, 89, 130, 147)
allocate `uint8_t buf[65536]` on the stack. This is ~64KB per call, and some
functions have multiple such buffers. Risk of stack overflow on constrained
environments (Android, embedded).

## Steps
1. Replace stack buffers with heap allocation (malloc + RAII or explicit free)
2. Or reduce buffer size with dynamic growth strategy
3. Ensure all error paths clean up properly

## Estimate
~30 lines

## Dependencies
None

## Reviewed — 2026-04-16
Pass. Confirmed all 64KB stack buffers in mtproto_rpc.c replaced with `RAII_STRING uint8_t *buf = malloc(RPC_BUF_SIZE)` (lines 49, 97, 126, 144). RPC_BUF_SIZE=65536 centralised as macro. Stack usage now O(1) per call.
