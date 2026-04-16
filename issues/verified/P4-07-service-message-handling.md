# Handle service messages: bad_msg_notification, new_session_created, msgs_ack

## Description
`api_call.c` now retries on `bad_server_salt`, but the MTProto spec
defines several more service messages that can appear unsolicited in an
encrypted response, interleaved with the actual RPC result:

- `bad_msg_notification#a7eff811` — msg_id / seq_no problems.
- `new_session_created#9ec20908` — server resets session state.
- `msgs_ack#62d6b459` — server acks previous messages (we should
  track these to know what it received).
- `pong#347773c5` — response to our (future) ping.

Today any of these arriving first causes `api_call` to parse garbage and
fail.

## Goal
After `rpc_recv_encrypted`, inspect the top constructor; if it is one of
the service types, handle it (update salt / log / skip) and loop back
to `rpc_recv_encrypted` until a real `rpc_result` (or another
terminating type) arrives.

## Steps
1. Factor the post-recv dispatch out of `api_call_once` into a loop.
2. Handle each of the four above constructors in a helper.
3. Cap the loop at N iterations (e.g. 8) to prevent a runaway server.
4. Unit-test by queueing a service message then the real response.

## Estimate
~150 lines incl. tests.

## Dependencies
None — builds on existing `maybe_handle_bad_salt()` pattern.

## Verified — 2026-04-16
- `src/infrastructure/api_call.c` now runs a recv loop that classifies
  each decrypted frame: SVC_RESULT passes through, SVC_BAD_SALT
  signals a retry with the new salt, SVC_SKIP drains the frame
  (new_session_created / msgs_ack / pong) and reads again,
  SVC_ERROR aborts (bad_msg_notification included).
- Loop capped at 8 service frames to guard against a runaway server.
- `tests/unit/test_api_call.c::test_new_session_created_skipped`
  covers the new_session_created path and confirms the salt is
  taken from the received frame.
- `bad_server_salt` continues to trigger a one-shot retry in the
  caller (api_call) — unchanged.

Tests: 1787 -> 1789. Valgrind: 0 leaks. Zero warnings.
