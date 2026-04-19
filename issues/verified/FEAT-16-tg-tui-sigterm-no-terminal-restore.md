# FEAT-16 — tg-tui --tui: SIGTERM does not restore terminal mode

## Gap
US-11 acceptance: "Starts without full-screen flicker; restores terminal
cleanly on SIGINT."

`src/main/tg_tui.c:run_tui_loop()` uses `RAII_TERM_RAW` (line 720) which
calls `terminal_raw_exit()` via `__attribute__((cleanup(...)))` on normal
function return. However, `SIGTERM` causes the process to exit immediately
via the default handler, bypassing all stack-frame cleanups. The terminal
is left in raw/no-echo mode, breaking the user's shell session.

Similarly, SIGINT in `--tui` mode does not have a handler; the REPL mode
relies on `rl_readline` returning -1 on Ctrl-C, but the TUI loop uses
`terminal_wait_key` with no SIGINT guard.

## Scope
1. Register a `SIGTERM` handler (and optionally `SIGINT`) in
   `run_tui_loop` via `sigaction` that calls `terminal_raw_exit` and
   then re-raises the signal (default handler) so the shell sees the
   correct exit status.
2. Use `atexit` or the existing `RAII_TERM_RAW` + signal handler
   pattern to ensure raw mode is always exited.
3. Document in `man/tg-tui.1` SIGNALS section (see DOC-12).
4. PTY test (see PTY-04) that sends SIGTERM to the `--tui` child and
   verifies the master side reads a clean screen reset (no raw mode
   artefacts).

## Acceptance
- `kill <tg-tui-pid>` while in `--tui` mode leaves the terminal in
  cooked mode (shell prompt is usable without `reset`).
- SIGINT in `--tui` mode also restores the terminal.
- REPL mode (no `--tui`) is unaffected.
