# TEST-82 — Functional coverage for transport resilience

## Gap
`src/infrastructure/transport.c` error / partial-I/O paths are
uncovered: short writes, short reads, EINTR/EAGAIN retry,
connect-refused, mid-RPC disconnect, SIGPIPE ignore.

## Scope
Mock-socket extensions (in `tests/mocks/socket.c`):
- `mock_socket_set_send_fragment(step)` — every `sys_send` writes
  at most `step` bytes.
- `mock_socket_set_recv_fragment(step)` — every `sys_recv` reads
  at most `step` bytes.
- `mock_socket_inject_eintr_next()` / `mock_socket_inject_eagain_next()`.
- `mock_socket_kill_on_next_recv()` — return 0 bytes (EOF) next
  call.
- `mock_socket_refuse_connect()` — `sys_connect` returns ECONNREFUSED.

New suite `tests/functional/test_transport_resilience.c`:

1. `test_connect_refused_is_fatal_with_clean_exit` — bootstrap
   fails, exit 1, stderr includes "connection refused".
2. `test_partial_send_retries_to_completion` — fragment step 16;
   a 1024-byte payload still arrives intact.
3. `test_partial_recv_reassembles_frame` — fragment step 16; a
   RPC answer is parsed correctly.
4. `test_eintr_is_silent_retry` — one EINTR injection during
   send; the RPC still completes.
5. `test_eagain_is_silent_retry` — same for EAGAIN.
6. `test_mid_rpc_disconnect_reconnects` — `watch` loop sees EOF,
   reconnects on the next poll, resumes.
7. `test_sigpipe_is_ignored` — parent process pipes watch to
   `head -c 100`; the watch process does NOT die of SIGPIPE.

## Acceptance
- 7 tests pass under ASAN + Valgrind.
- Functional coverage of `transport.c` ≥ 95 % (from 73 %).

## Dependencies
US-31 (the story). Touches mocks/socket.c.
