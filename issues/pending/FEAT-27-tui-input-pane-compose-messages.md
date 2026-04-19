# FEAT-27 — tg-tui curses mode: input pane for composing messages

## Gap
`run_tui_loop` in `tg_tui.c` presents a read-only three-pane UI:
dialog list, history pane, status row.  There is no input pane and
no way to send a message without dropping to REPL mode.

Users cannot compose without exiting the full-screen view.  US-11 v2
lists an input pane as a goal, but the pane is absent.

## Scope
1. Add `src/tui/input_pane.{h,c}` with:
   - fixed 3-row editor at the bottom of the history pane region.
   - Cursor visible, line editing via the existing `readline.c`
     routines adapted to operate on the pane buffer.
   - Enter → submit; compose area cleared; `send` RPC fired.
2. Wire a `/` keystroke in dialog pane to focus the input pane.
3. Document the binding in `man/tg-tui.1` and in `print_help()`.

## Acceptance
- User presses `/` after opening a dialog, types "hi", presses Enter.
- `mt_server_rpc_call_count()` for `messages.sendMessage` increments
  by 1.
- History pane refreshes with the new outgoing message.
- Paired test: `tests/functional/pty/test_tui_send_message.c`
  (part of PTY-11 family).

## Dependencies
- PTY-11 (open dialog baseline).
- Screen layout needs to grow support for 4-region splits.
