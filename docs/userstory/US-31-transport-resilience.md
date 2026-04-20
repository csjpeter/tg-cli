# US-31 — Transport resilience: drops, partial I/O, reconnect

Applies to: all three binaries.

**Status:** gap — `src/infrastructure/transport.c` is 73 %
functionally covered. Uncovered lines are all in the error /
partial-I/O paths: `transport_connect` failure, `transport_send`
short-write retry, `transport_recv` EAGAIN, mid-session TCP close.
No functional test drops the connection mid-RPC.

## Story
As a user on a mobile tether, flaky VPN, or long-running `watch`
process, I want tg-cli to handle the network the way ssh does:
retry short reads/writes, distinguish transient from fatal
errors, and — when the socket dies mid-RPC — reconnect and
resume instead of exiting with a cryptic errno.

## Uncovered practical paths
- **connect() refused / unreachable:** DNS resolves, TCP connect
  fails → distinct exit code + stderr.
- **Partial send():** kernel accepts only N < total bytes → loop
  finishes the write rather than dropping the tail.
- **Partial recv():** reads complete only after two or three
  `recv()` calls → parsers must not see a truncated frame.
- **EINTR / EAGAIN during send/recv:** retry silently unless the
  overall deadline is hit.
- **Mid-RPC disconnect:** the peer closes the socket between the
  request and the response → `watch`, `history --limit 1000`,
  and `upload` all reconnect on the next poll.
- **SIGPIPE:** prod code ignores SIGPIPE so a `head -c 100 | tg-cli
  watch` termination does not kill the process with signal 13
  (already handled; needs a functional assertion).
- **Slow server:** deliberate server-side delay (mock injects
  sleep) triggers the existing read timeout cleanly.

## Acceptance
- Mock-socket extensions:
  - `mock_socket_set_send_fragment(step)` — emit partial writes.
  - `mock_socket_set_recv_fragment(step)`.
  - `mock_socket_kill_on_next_recv()`.
  - `mock_socket_refuse_connect()`.
- New suite `tests/functional/test_transport_resilience.c`:
  - `test_connect_refused_is_fatal_with_clean_exit`
  - `test_partial_send_retries_to_completion`
  - `test_partial_recv_reassembles_frame`
  - `test_eintr_eagain_are_silent_retries`
  - `test_mid_rpc_disconnect_reconnects`
  - `test_sigpipe_is_ignored`
- Functional coverage of `transport.c` ≥ 95 % (from 73 %).

## Dependencies
ADR-0004 (DI). US-07 (watch) benefits directly. Pairs with
TEST-66 (dc-migration transport failover).
