# TEST-88 — Functional coverage for server-side service frames

## Gap
`src/infrastructure/api_call.c::classify_service_frame` handles
five non-result CRCs (bad_server_salt, bad_msg_notification,
new_session_created, msgs_ack, pong) but no functional test
injects them. Salt rotation on long-running `watch` and TUI
sessions is the most likely untested failure mode in the wild.

## Scope
Mock-server extensions:
- `mt_server_reply_bad_server_salt(uint64_t new_salt)`
- `mt_server_reply_new_session_created(void)`
- `mt_server_reply_msgs_ack(const uint64_t *ids, size_t n)`
- `mt_server_reply_pong(uint64_t msg_id, uint64_t ping_id)`
- `mt_server_reply_bad_msg_notification(uint64_t bad_id, int code)`
- `mt_server_stack_service_frames(size_t count)` — enqueues
  that many `msgs_ack` frames ahead of the real answer.

New suite `tests/functional/test_service_frames.c`:

1. `test_bad_server_salt_auto_retries_and_succeeds` — seed sends
   bad_server_salt then the real `help.getConfig` result; the
   domain call returns success; `s.server_salt` is updated.
2. `test_new_session_created_refreshes_salt`.
3. `test_msgs_ack_is_transparent` — ack frames precede the real
   result; caller sees only the result.
4. `test_pong_is_transparent`.
5. `test_bad_msg_notification_surfaces_error_without_dropping_session`
   — the specific RPC fails, the session struct retains its
   auth_key and salt.
6. `test_service_frame_storm_bails_at_limit` — 9 service frames
   stacked; call returns -1 after hitting `SERVICE_FRAME_LIMIT`.

## Acceptance
- 6 tests pass under ASAN + Valgrind.
- Functional coverage of `api_call.c` ≥ 90 %.
- `tg-cli-ro watch` benchmark: ≥ 1 h runtime with periodic
  salt rotation injection completes without errors.

## Dependencies
US-37 (the story). ADR-0007 (mock server).
