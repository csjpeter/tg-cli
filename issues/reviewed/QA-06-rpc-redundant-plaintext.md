# Redundant plaintext construction in rpc_send_encrypted

## Description
`rpc_send_encrypted()` in `src/infrastructure/mtproto_rpc.c` (lines 78-106)
builds the plaintext TL payload twice: once for encryption, once for msg_key
computation. This violates DRY and wastes CPU cycles.

## Steps
1. Build plaintext once
2. Compute msg_key from the already-built buffer
3. Remove the duplicate TlWriter

## Estimate
~10 lines (net reduction)

## Dependencies
None

## Reviewed — 2026-04-16
Pass. Confirmed rpc_send_encrypted (mtproto_rpc.c lines 83-101) builds the plaintext TlWriter once; msg_key computed from plain.data/plain.len directly; encryption reuses the same buffer. No duplicate TlWriter remains.
