# Transport abridged 3-byte prefix sends only 2 length bytes

## Description
`transport_send()` in `src/infrastructure/transport.c` (lines 54-58) handles
the extended length prefix for payloads >= 0x7F 4-byte units:

```c
prefix[0] = 0x7F;
prefix[1] = (uint8_t)(wire_len & 0xFF);
prefix[2] = (uint8_t)((wire_len >> 8) & 0xFF);
if (sys_socket_send(t->fd, prefix, 3) != 3) return -1;
```

The MTProto Abridged spec says: when the first byte is 0x7F, the next **3 bytes**
encode the length as LE24. But this code only encodes 2 bytes (LE16), missing
`(wire_len >> 16) & 0xFF`. This caps the maximum payload at 65535 * 4 = 262140
bytes. Larger payloads silently truncate the length.

The same issue exists in `transport_recv()` (lines 83-86) which only reads 2
extra bytes instead of 3.

## Severity
HIGH — silent data loss for payloads > 256KB; recv/send length mismatch for
large server responses (e.g., file downloads, large chat histories).

## Steps
1. Fix `transport_send`: send 4 bytes total (0x7F + LE24):
   ```c
   prefix[0] = 0x7F;
   prefix[1] = (uint8_t)(wire_len & 0xFF);
   prefix[2] = (uint8_t)((wire_len >> 8) & 0xFF);
   prefix[3] = (uint8_t)((wire_len >> 16) & 0xFF);
   if (sys_socket_send(t->fd, prefix, 4) != 4) return -1;
   ```
2. Fix `transport_recv`: read 3 extra bytes, decode LE24
3. Add unit tests for payloads > 0x7F * 4 bytes and > 0xFFFF * 4 bytes

## Estimate
~15 lines code + ~25 lines tests

## Dependencies
None

## Verified — 2026-04-16
- `src/infrastructure/transport.c::transport_send` extended-prefix
  path now emits 4 bytes total (0x7F + LE24), closing the silent
  truncation above 256 KB payloads.
- `transport_recv` now reads 3 extra bytes after the 0x7F marker
  and decodes them as LE24.
- `tests/unit/test_phase2.c`:
  - Existing wide-prefix test updated from 3 → 4 bytes.
  - New `test_transport_send_extended_prefix_wide` exercises
    payload = 0x040000 (3rd byte carries the 17th bit).
  - New `test_transport_recv_extended_prefix_wide` verifies the
    parsed wire_len overflows a small buffer correctly.
  - Existing truncated-prefix test updated to the new threshold.

Tests: 1803 -> 1812. Valgrind: 0 leaks. Zero warnings.

Related: QA-23 (transport-recv-3byte-prefix) is the same wire defect
on the receive side, now also fixed by this change.
