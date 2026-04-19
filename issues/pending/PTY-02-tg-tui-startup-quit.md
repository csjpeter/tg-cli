# PTY-02 — PTY test: tg-tui --tui startup, dialog list, and quit

## Gap
US-11 acceptance:
- "Starts without full-screen flicker."
- "All terminal output UTF-8; supports CJK width via wcwidth."

No test verifies that `tg-tui --tui` (a) paints the three-pane layout
to a real TTY, (b) shows the expected status-row hint text, or (c)
exits cleanly on `q` restoring the terminal to cooked mode.

Depends on PTY-01 (libptytest).

## Scope
1. Add `tests/functional/pty/test_tui_startup.c`:
   - Pre-seed a session using the in-process mock server pattern
     (same as `test_tui_e2e.c`).
   - Launch `bin/tg-tui --tui` inside a PTY (80×24).
   - Wait for the status row to contain `[dialogs]`.
   - Assert `pty_screen_contains(s, "[dialogs]")` is true.
   - Send `q` to quit; wait for child exit.
   - Assert exit code 0.
2. Add `test_tui_terminal_restored`: after the child exits, check that
   the master side is NOT in raw mode by writing a printable char and
   verifying it echoes (cooked-mode echo).

## Acceptance
- Both tests pass under ASAN.
- PTY master sees the correct screen state without manual `reset`.
- Test does not hang (uses `pty_wait_for` with a 5 s timeout).
