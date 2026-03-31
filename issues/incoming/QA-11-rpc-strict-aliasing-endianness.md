# Strict-aliasing violation and endianness bug in rpc_send_encrypted

## Description
`rpc_send_encrypted()` in `src/infrastructure/mtproto_rpc.c` (line 110) computes
auth_key_id via:

```c
tl_write_uint64(&wire, *(uint64_t *)(key_hash + 24));
```

This has two problems:
1. **Strict-aliasing UB:** Casting `uint8_t *` to `uint64_t *` and dereferencing
   violates C11 strict aliasing rules. The compiler may optimize this incorrectly
   at `-O2` or higher.
2. **Endianness bug:** On big-endian platforms the byte order is reversed,
   producing a wrong auth_key_id. The server will reject the message.

## Severity
CRITICAL — undefined behavior in optimized builds; breaks on big-endian platforms.

## Steps
1. Replace pointer cast with `memcpy`:
   ```c
   uint64_t auth_key_id;
   memcpy(&auth_key_id, key_hash + 24, 8);
   tl_write_uint64(&wire, auth_key_id);
   ```
   (`tl_write_uint64` already handles LE encoding.)
2. Add a comment noting that auth_key_id is the **last 8 bytes** of SHA256(auth_key)

## Estimate
~5 lines

## Dependencies
None
