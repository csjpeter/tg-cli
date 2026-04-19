# PTY-05 — PTY test: tg-tui REPL Ctrl-C exits without raw-mode artefacts

## Gap
US-11 acceptance: "Restores terminal cleanly on SIGINT."

The REPL loop in `src/main/tg_tui.c:repl()` returns 0 when
`rl_readline` returns -1 (which happens on Ctrl-C / EOF). This
eventually falls through to `transport_close` + `app_shutdown`. The
terminal is in cooked mode during REPL (raw mode is only entered in
`run_tui_loop`), so cleanup is simpler — but no PTY test confirms this.

In `--tui` mode, SIGINT is handled by the TERM_KEY_QUIT / ESC path in
`tui_app_handle_key`; however, if the process is killed by SIGINT from
outside (not from `q`/ESC keypress) the RAII_TERM_RAW may or may not
fire (RAII cleanup via GNU cleanup attribute does run on normal return,
but signal delivery into blocking `read()` is implementation-defined).

Depends on PTY-01 (libptytest).

## Scope
Add `tests/functional/pty/test_repl_ctrl_c.c`:
1. Launch `tg-tui` (REPL mode); wait for `tg> ` prompt.
2. Send `Ctrl-C` (byte 0x03 via `pty_send_key(s, PTY_KEY_CTRL_C)`).
3. Assert child process exits within 1 second.
4. Assert exit code is 0 (graceful).
5. After child exits, verify terminal is not in raw mode by checking
   that a printable char typed on the master echoes back (cooked mode).

Also add `test_tui_mode_ctrl_c`: launch with `--tui`, send `Ctrl-C`,
verify clean exit and terminal restoration.

## Acceptance
- Both tests pass under ASAN.
- No `tty_raw_mode` left-over after child exits.
- Child exits within the 1-second timeout.
