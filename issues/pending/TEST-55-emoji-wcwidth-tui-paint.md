# TEST-55 — Functional test: emoji + CJK dialog titles paint without overflow

## Gap
`src/core/wcwidth.c` implements Unicode 15.1 width tables.  The TUI
relies on correct `core_wcwidth` for column alignment of dialog
titles and history messages.  No FT exercises:

- A dialog title containing a single emoji (U+1F600 "😀") — width 2.
- A title mixing ASCII + emoji + CJK ideographs.
- A title with a combining character (U+0301 "◌́") — width 0.
- A title longer than the pane width and truncated mid-emoji.

A regression in wcwidth (e.g. wrong binary-search boundary) would
corrupt the dialog pane's column alignment silently.

## Scope
Create `tests/functional/test_tui_wcwidth_paint.c`:
- Seed 3 dialogs:
  - Title "alice 😀" (7 codepoints, 8 display cols).
  - Title "北京" (2 codepoints, 4 display cols).
  - Title "é" (2 codepoints including combiner, 1 display col).
- Call `dialog_pane_refresh`, then `dialog_pane_paint` at 40 cols.
- Capture the pane buffer (via `screen_buffer_at`) and assert the
  column after each title aligns to the expected boundary.
- Repeat at 20 cols — truncation must not split a multi-byte
  codepoint nor leave a stray combining mark.

## Acceptance
- 3 seed titles rendered at correct column counts at 40 cols and
  20 cols.
- No UTF-8 sequence split across the truncation boundary.

## Dependencies
- FEAT-23 (Windows wcwidth now delegates to core_wcwidth) already
  verified.
