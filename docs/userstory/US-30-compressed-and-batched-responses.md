# US-30 — Gzip-packed and msg_container response handling

Applies to: all three binaries.

**Status:** gap — `src/infrastructure/mtproto_rpc.c::rpc_unwrap_gzip`
and `rpc_parse_container` are 0 %/0 % functional covered. Unit
tests validate the parsers on fixed buffers. The mock server never
wraps its responses in `msg_container` or returns `gzip_packed`
bodies, so the integrated path is untested.

## Story
Real Telegram servers return:
- **gzip_packed** bodies for large responses (e.g. big channel
  histories, large search results, huge contact lists). The body
  is the `gzip_packed { bytes }` constructor; the client must
  decompress with tinf before parsing.
- **msg_container** envelopes batching multiple server messages
  into a single TCP frame — very common immediately after connect
  (`new_session_created` + pending salts + one RPC answer).

I want tg-cli to handle both transparently so large queries and
cold starts don't corrupt state.

## Uncovered practical paths
- **gzip_packed rpc_result** wrapping a `messages.messages` with
  200 KB of text bodies. The happy round trip through
  `rpc_unwrap_gzip` must reproduce a byte-identical TL buffer.
- **Corrupt gzip stream** → `rpc_unwrap_gzip` returns -1, caller
  logs and fails without reading past the end.
- **`msg_container` with three children:** `new_session_created`,
  `rpc_result`, `msgs_ack` → each child dispatched independently.
- **Nested container** (illegal but has occurred in the wild) →
  parser rejects cleanly.
- **`msg_container` with mid-body bad alignment** (body_len not
  divisible by 4) → parser rejects, no garbled follow-on reads.

## Acceptance
- Mock server gains:
  - `mt_server_reply_gzip_wrapped_result(body)` — produces
    `rpc_result { gzip_packed { body } }`.
  - `mt_server_reply_msg_container(child1, child2, …)`.
  - `mt_server_reply_gzip_corrupt()`.
- New suite `tests/functional/test_rpc_envelope.c`:
  - `test_gzip_packed_history_decoded`
  - `test_gzip_corrupt_surfaces_error`
  - `test_msg_container_three_children_dispatch`
  - `test_msg_container_unaligned_body_rejected`
- Functional coverage of `mtproto_rpc.c` ≥ 85 % (from 67 %).

## Dependencies
Adjacent to US-15 (cross-DC) — both rely on mock-server extensions.
Uses the bundled `tinf` gzip decompressor (already vendored).
