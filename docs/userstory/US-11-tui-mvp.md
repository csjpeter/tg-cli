# US-11 — Interactive TUI (read-only MVP)

Applies to: `tg-tui`.

## Story
As a user I want an interactive terminal UI where I can navigate my dialogs
and read the history of the selected chat, with new messages appearing as
they arrive.

## Scope
- Layout: left pane = dialog list; right pane = message history of selected
  dialog; bottom line = status / command buffer.
- Navigation: arrow keys, `j/k`, `PgUp/PgDn`, `Enter` to open, `q` to quit.
- Uses the same domain functions as `tg-cli-ro` (US-04, US-06, US-07).
- Input handled via existing `readline.c` machinery (extended as needed).

## Non-goals (v1)
- Sending/editing/deleting — handled in US-12.
- Mouse support.

## Acceptance
- Starts without full-screen flicker; restores terminal cleanly on SIGINT.
- New messages for the open chat appear within the poll interval.
- All terminal output UTF-8; supports CJK width via `wcwidth`.

## Dependencies
US-04 · US-06 · US-07 · US-02 (headless test harness to cover render logic).
