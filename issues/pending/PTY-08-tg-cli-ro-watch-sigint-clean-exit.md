# PTY-08 — PTY test: tg-cli-ro watch terminates cleanly on Ctrl-C

## Gap
`tg_cli_ro.c:cmd_watch` installs a `SIGINT` handler that sets `g_stop`
and exits the polling loop.  The man page SIGNALS section documents
this contract, but no automated test verifies it.  A regression (for
example forgetting to call `transport_close` on the quit path, or
leaving the output flushed without a trailing newline) would go
undetected.

## Scope
1. Add `tests/functional/pty/test_tg_cli_ro_watch_sigint.c`:
   - Seed mock server with an empty difference so `watch` enters the
     sleep branch.
   - Launch `bin/tg-cli-ro watch --interval 2` under PTY.
   - Wait for the line "no new messages" to appear on the master.
   - Send SIGINT to the child via `kill(child_pid, SIGINT)`.
   - Assert child exits with code 0 within 3 seconds (clean break
     out of the sleep loop).
2. Sub-assertion: after exit, no zombie socket remains open
   (harvested by `mt_server_reset()`).

## Acceptance
- Test passes under ASAN and Valgrind.
- If the 1-second poll window in `cmd_watch` ever grows, Ctrl-C
  response stays under 3 s — the test catches any regression.

## Dependencies
- PTY-01.
- Uses `pty_tel_stub` so the mock server runs in-process.
