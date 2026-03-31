# transport_recv reads only 2 bytes for extended length prefix

## Description
`transport_recv()` in `src/infrastructure/transport.c` (lines 83-86) handles
the extended length prefix (first byte == 0x7F) by reading only 2 extra bytes:

```c
uint8_t extra[2];
r = sys_socket_recv(t->fd, extra, 2);
wire_len = (size_t)extra[0] | ((size_t)extra[1] << 8);
```

The MTProto Abridged spec defines: when first byte is 0x7F, the next **3 bytes**
encode LE24 length. This code reads only LE16, missing the high byte.

This is the recv-side counterpart of QA-15. Together they mean:
- Sends with `wire_len > 0xFFFF` encode wrong length
- Receives with `wire_len > 0xFFFF` decode wrong length
- Protocol desynchronization for large payloads

## Severity
LOW (paired with QA-15 which covers the full issue at HIGH severity).

## Steps
1. Read 3 extra bytes instead of 2
2. Decode: `wire_len = extra[0] | (extra[1] << 8) | (extra[2] << 16)`
3. Covered by QA-15 tests

## Estimate
~5 lines (fix alongside QA-15)

## Dependencies
QA-15 (should be fixed together)
