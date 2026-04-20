# US-37 — Server service frames: salt rotation, new_session, ack, pong

Applies to: all three binaries.

**Status:** gap — `src/infrastructure/api_call.c::classify_service_frame`
dispatches five non-result CRCs: `bad_server_salt`,
`bad_msg_notification`, `new_session_created`, `msgs_ack`, `pong`.
The function is 0 % functional covered because the mock server
never injects these frames. On long-running processes (`watch`,
interactive TUI) salt rotation is a daily occurrence — untested
rotation is the most common "silent cluster of errors overnight"
failure mode.

## Story
As a user keeping `tg-cli-ro watch` or an interactive tg-tui open
all day, I want the client to handle salt rotation, implicit
session regeneration, idle pings, and acks automatically, without
surfacing cryptic transient errors or dropping the connection.

## Uncovered practical paths
- **bad_server_salt:** server issues a new salt; client reads
  the new salt (bytes 20–27), swaps it in, retries the in-flight
  RPC on the *same* connection, receives the real result.
  Practical: happens ~hourly on long sessions.
- **new_session_created:** after a long idle or a server restart,
  the server announces a fresh session with a new salt. Client
  picks up the salt and continues without reconnecting.
- **msgs_ack:** server acks pending messages; the parser must
  skip past it to the real result behind. Common on busy accounts.
- **pong:** response to our occasional ping keep-alive; ignored.
- **bad_msg_notification:** msg_id / seqno disagreement (very
  rare, indicative of a client bug); logged at WARN, current RPC
  fails cleanly but the session is not discarded.
- **Service-frame storm:** up to `SERVICE_FRAME_LIMIT` (8) frames
  can precede the real answer; the 9th is treated as a failure.

## Acceptance
- Mock-server extensions:
  - `mt_server_reply_bad_server_salt(new_salt)` — one-shot, the
    next sent RPC sees the bad-salt frame before any retry.
  - `mt_server_reply_new_session_created()`.
  - `mt_server_reply_msgs_ack(msg_ids, n)`.
  - `mt_server_reply_pong(msg_id, ping_id)`.
  - `mt_server_reply_bad_msg_notification(bad_msg_id, code)`.
  - `mt_server_stack_service_frames(count)` — stress-test up to
    the limit.
- New suite `tests/functional/test_service_frames.c`:
  1. `test_bad_server_salt_auto_retries_and_succeeds`
  2. `test_new_session_created_refreshes_salt`
  3. `test_msgs_ack_is_transparent`
  4. `test_pong_is_transparent`
  5. `test_bad_msg_notification_surfaces_error_without_dropping_session`
  6. `test_service_frame_storm_bails_at_limit`
- Functional coverage of `api_call.c` ≥ 90 % (from ~79 %).

## Dependencies
US-07 (watch), US-31 (transport resilience). Shares mock-server
infrastructure with US-30.
