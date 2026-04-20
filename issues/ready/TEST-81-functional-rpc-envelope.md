# TEST-81 — Functional coverage for gzip_packed + msg_container envelopes

## Gap
`src/infrastructure/mtproto_rpc.c` functions `rpc_unwrap_gzip` and
`rpc_parse_container` are 0 % functional covered. Mock server
never emits compressed bodies or multi-message containers, so
real-world server responses (which routinely use both) are not
exercised end-to-end.

## Scope
Mock-server extensions:
- `mt_server_reply_gzip_wrapped_result(body, body_len)` — wrap
  the body into `rpc_result { gzip_packed { … } }`.
- `mt_server_reply_gzip_corrupt()`.
- `mt_server_reply_msg_container(children[], n)` — enclose N
  serialised children in an `msg_container` envelope.

New suite `tests/functional/test_rpc_envelope.c`:

1. `test_gzip_packed_history_roundtrip` — large `messages.messages`
   (≈200 KB of text) arrives gzip-packed; history output matches
   the unpacked fixture byte for byte.
2. `test_gzip_packed_small_below_threshold_still_works` — tiny
   gzip body (edge case for tinf min-frame).
3. `test_gzip_corrupt_surfaces_error` — `rpc_unwrap_gzip` returns
   -1, domain call returns -1, stderr mentions "decompress".
4. `test_msg_container_with_new_session_plus_rpc_result` — two
   children dispatched independently; both effects visible.
5. `test_msg_container_with_msgs_ack_interleaved` — ack child
   does not disturb the subsequent rpc_result.
6. `test_msg_container_unaligned_body_rejected` — body_len not
   divisible by 4; parser returns -1.
7. `test_nested_container_rejected` — container inside a
   container (not legal per spec); clean refusal.

## Acceptance
- 7 tests pass under ASAN.
- Functional coverage of `mtproto_rpc.c` ≥ 85 % (from 67 %).
- No regression in unit-level rpc parser tests.

## Dependencies
US-30 (the story). ADR-0007 (mock server).
