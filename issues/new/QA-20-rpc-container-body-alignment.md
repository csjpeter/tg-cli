# rpc_parse_container does not validate body_len 4-byte alignment

## Description
`rpc_parse_container()` in `src/infrastructure/mtproto_rpc.c` (line 253)
reads `body_len` from each message in a `msg_container` but does not verify
that `body_len` is a multiple of 4 bytes.

TL-serialized message bodies are always 4-byte aligned. A malformed container
with odd `body_len` causes `tl_read_skip()` to advance the reader position
to a non-aligned offset, misaligning all subsequent message reads. This
silently produces garbage data for remaining messages in the container.

## Severity
MEDIUM — silent data corruption when parsing malformed containers.

## Steps
1. Add check: `if (msgs[i].body_len % 4 != 0) return -1;`
2. Add unit test with a container containing odd-length body

## Estimate
~5 lines code + ~15 lines tests

## Dependencies
None
