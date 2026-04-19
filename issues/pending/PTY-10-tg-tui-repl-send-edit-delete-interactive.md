# PTY-10 — PTY test: tg-tui REPL write commands (send / edit / delete / reply)

## Gap
`tg_tui.c` dispatches REPL commands `send`, `reply`, `edit`, `delete`,
`forward`, `upload`, `read`, `poll` via string compare and prints a
one-line status.  These interactive flows are never tested via PTY —
only the underlying domain layer is tested via the non-interactive
FTs.  A regression in the arg parsing (`split_rest`) or in the status
print would be invisible.

## Scope
1. Add `tests/functional/pty/test_tg_tui_repl_write.c` covering four
   sub-tests:
   a. `send @user hello world` → PTY master reads "sent, id=…".
   b. `reply @user 42 hello` → PTY master reads "sent, id=… (reply to 42)".
   c. `edit @user 42 goodbye` → PTY master reads "edited 42".
   d. `delete @user 42 revoke` → PTY master reads "deleted 42 (revoke)".
2. Each sub-test uses the in-process mock to serve the matching
   RPC and asserts the domain RPC was actually invoked once.

## Acceptance
- All four sub-tests pass under ASAN.
- Negative cases exercised: missing msg_id → usage text printed,
  zero RPCs issued.

## Dependencies
- PTY-01.
- Mock responders for `messages.sendMessage`, `messages.editMessage`,
  `messages.deleteMessages` (already present in tests/mocks).
