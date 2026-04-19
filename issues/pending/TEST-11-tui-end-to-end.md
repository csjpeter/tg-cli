# TEST-11 — functional test for the tg-tui --tui mode

## Gap
US-11 acceptance is exercised by unit tests only
(`test_tui_app.c`, `test_tui_dialog_pane.c`,
`test_tui_history_pane.c`). No functional test drives the full
`TuiApp` loop against the mock Telegram server: dialog fetch → select
→ history fetch → render cycle.

## Scope
1. Seed session, register responders for `messages.getDialogs` and
   `messages.getHistory`.
2. Drive `tui_app_handle_*` through a synthetic keypress sequence
   (`j`, `Enter`, `q`) and `tui_app_paint` into an `open_memstream`
   buffer.
3. Assert the ANSI output contains the expected dialog title and, in
   the history pane, the canned message text.

## Acceptance
- Test green; proves dialog-load → history-load pipeline works
  end-to-end under real MTProto framing.
