# FEAT-36 — TUI: preserve dialog + history selection across SIGWINCH

## Gap
`run_tui_loop` handles SIGWINCH via `tui_app_resize`, which re-lays
out panes and repaints.  Code inspection suggests the list_view
selection index is kept, but there is no test confirming:

1. Selected dialog remains the same item (by peer_id) after a resize
   from 80x24 to 120x40.
2. If the pane gets narrower and the selected row would scroll
   off-screen, it is re-scrolled into view.
3. If an open history pane has a scroll offset, it is preserved by
   row index (not absolute pixel position).

If these are already implemented, the behaviour is not documented;
if not, a resize mid-read throws the user's scroll position away.

## Scope
1. Audit `tui_app_resize` for selection preservation.
2. If missing, add:
   - `list_view_remember_selected()` before resize,
   - `list_view_restore_selected()` after resize,
   - with scroll clamp to keep selection visible.
3. Add `tests/functional/pty/test_tui_resize_preserve_selection.c`
   extending PTY-04:
   - Open dialog list, move selection to row 5, open history,
     scroll history down 3 rows.
   - Send SIGWINCH to resize to smaller size.
   - Assert selected dialog still row 5 by peer_id.
   - Assert history scroll offset re-computed so the same top
     message is visible.

## Acceptance
- Resize does not lose selection or scroll.
- Paired PTY test passes.

## Dependencies
- PTY-04 (resize baseline verified).
