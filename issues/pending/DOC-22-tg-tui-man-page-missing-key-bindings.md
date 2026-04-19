# DOC-22 — tg-tui man page missing curses-mode key binding reference

## Gap
`man/tg-tui.1` has a "MODES" section describing the REPL and
`--tui` (curses) mode, and a "REPL COMMANDS" section for the
read/write commands in REPL.  But it lacks a reference for the
keyboard shortcuts available inside curses mode:

- `j`/`k` — move selection down/up in the active pane.
- `g`/`G` — jump to first/last item.
- `Enter` — open the selected dialog (history pane).
- `q` — quit.
- `Tab` — switch focus between dialog pane and history pane
  (if implemented; check source).
- `Ctrl-C` — graceful quit (terminal restored).
- SIGWINCH — live resize.

Users cannot discover these from the man page.

## Scope
1. Add `.SS Key bindings (--tui mode)` under MODES or as a new
   top-level section `.SH KEY BINDINGS`.
2. Source the key list from `tui_app_handle_key()` in
   `src/tui/app.c` so the doc matches code.
3. Also document that TUI-mode key input is exclusive:
   a key in the input pane consumes the keystroke — there is no
   REPL-style `send` shortcut.

## Acceptance
- Every key handled by `tui_app_handle_key` is present in the man
  page section.
- Cross-reference: `PTY-11` test asserts the same key set.
