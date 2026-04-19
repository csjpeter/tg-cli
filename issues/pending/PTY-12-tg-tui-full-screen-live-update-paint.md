# PTY-12 — PTY test: tg-tui --tui repaints on live getDifference push

## Gap
`run_tui_loop` polls `updates.getDifference` every 5 s and, on a
non-empty diff, refreshes the dialog pane and (if a peer is opened)
the history pane.  Nothing tests this end-to-end with a PTY —
`test_tui_e2e.c` only pumps synthetic keys and does not exercise the
poll path.

If the poll code ever falls silent (e.g. a refactor that nukes the
repaint call), the TUI will appear frozen on live data and no test
will notice.

## Scope
1. Add `tests/functional/pty/test_tg_tui_live_update.c`:
   - Seed dialogs {A=@alice, B=@bob}, empty state.
   - Spawn `bin/tg-tui --tui` with `POLL_INTERVAL_MS=1000` via a
     test-only env override (add if missing).
   - Wait for dialogs to paint.
   - Arm the mock to return a non-empty `updates.difference` on next
     call: one new message on peer @alice.
   - Wait up to 3 s for the master to show a new-message indicator
     on the @alice row (unread count or `*`).
   - Send `q` to exit.

## Acceptance
- Dialog pane re-paints without user keypress within 3 s of the
  mock arming.
- Test does not hang on a clean run.

## Dependencies
- PTY-01.
- Small feature ticket possibly needed: expose `POLL_INTERVAL_MS`
  via env for tests.
