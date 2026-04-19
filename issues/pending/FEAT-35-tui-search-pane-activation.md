# FEAT-35 — tg-tui --tui mode: in-pane search activation

## Gap
`run_tui_loop` in `tg_tui.c` has no code path that lets a user
filter/search within the dialog pane or within the open history
pane.  The REPL's `search [<peer>] <query>` is unavailable in TUI
mode.  Heavy users scrolling through 100+ dialogs have no way to
jump to a specific chat without dropping to REPL.

## Scope
1. Add a `/` keybinding in dialog pane that opens a prompt at the
   status row: `search: `.  While in search mode:
   - Typed chars filter the dialog list in place (client-side match
     on title + username).
   - Enter locks the filter; Esc clears it; arrows still navigate.
2. In history pane: `/` opens "history search: ", submits to
   `domain_search_peer` and paints results in a dedicated pane or
   overlay.
3. Wire key handling through `tui_app_handle_key` extended with a
   new mode flag (`TUI_MODE_SEARCH`).

## Acceptance
- Pressing `/` in dialog pane reveals the input at status row.
- Typing narrows the dialog list instantly.
- Esc restores full list.
- Paired `tests/functional/pty/test_tui_search_pane.c` validates.

## Dependencies
- PTY-11 (opens dialogs baseline).
- DOC-22 key binding reference will need updating.
