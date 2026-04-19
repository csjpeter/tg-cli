# PTY-11 — PTY test: tg-tui --tui navigates and opens a dialog

## Gap
`test_tui_e2e.c` covers the TUI keypress sequence `j`, `Enter`, `q`
against the in-process pane state (no PTY).  `test_tui_startup.c`
(PTY) only verifies the initial "dialogs" hint and clean exit.

Nothing verifies the full loop: a real PTY spawns the binary, the
dialog pane is populated, the user moves the selection with `j`/`k`,
presses Enter, the history pane loads and paints a message from the
mock server, then the user presses `q` and exits cleanly.

## Scope
1. Add `tests/functional/pty/test_tg_tui_open_dialog.c`:
   - Mock: 3 dialogs, middle one with peer_id 42 and one message
     "hello from FT".
   - Spawn `bin/tg-tui --tui`.
   - Wait for dialog pane to contain the 3 titles.
   - Send `j` → selection moves to dialog 2.
   - Send Enter → status "loading…" appears.
   - Wait for history pane to contain "hello from FT".
   - Send `q` → clean exit with code 0.
2. Sibling: after Enter, send SIGWINCH to change rows to 40;
   assert the history pane is repainted at new height.

## Acceptance
- Tests pass under ASAN.
- History pane visible after Enter, showing the seeded message.
- No residual terminal escape bytes after exit (cooked mode).

## Dependencies
- PTY-01, PTY-02 (startup baseline), PTY-04 (SIGWINCH).
