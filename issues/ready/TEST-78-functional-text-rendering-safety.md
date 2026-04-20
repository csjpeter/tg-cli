# TEST-78 — Functional coverage for Unicode + ANSI-escape safety

## Gap
`src/tui/screen.c::utf8_decode`, `terminal_wcwidth`, and the SEC-01
control-character sanitisation block (screen.c L77–92) are
uncovered in functional tests. TEST-55 covers dialog *titles* at
correct widths but not message *bodies* with injection-like
payloads, malformed UTF-8, or RTL characters.

## Scope
New suite `tests/functional/test_text_rendering_safety.c` — 12
tests:

1. `test_plain_history_strips_ansi_csi` — body contains `\x1b[2J`,
   stdout has literal middle-dot, no raw 0x1B byte.
2. `test_plain_history_strips_osc_title` — body contains
   `\x1b]0;evil\x07`, stdout sanitised.
3. `test_plain_history_strips_bel_and_c1` — BEL (0x07), CSI 8-bit
   (0x9B), DEL (0x7F).
4. `test_plain_history_preserves_newline_and_tab` — these two are
   allowed to pass through.
5. `test_tui_pane_renders_emoji_message` — body "hi 😀" lands on
   3 display cols (width 2 + space + ASCII).
6. `test_tui_pane_renders_cjk_message` — "你好" → 4 cols.
7. `test_tui_pane_renders_rtl_message` — "שלום" rendered LTR at
   logical order (we do not do BiDi shaping, but no cell corrupts).
8. `test_tui_pane_zwj_cluster_stays_single_cell` — 👨‍👩‍👧.
9. `test_tui_pane_combining_mark_width_zero` — "é" (e + U+0301).
10. `test_malformed_utf8_replacement_char` — three invalid bytes
    become three U+FFFD, adjacent ASCII preserved.
11. `test_overlong_utf8_rejected` — 5-byte sequence → U+FFFD.
12. `test_utf8_bom_zero_width` — U+FEFF inside message is not
    printed as garbage.

Helper: `assert_no_raw_escape(char *buf, size_t len)` scans for
0x1B, 0x07, 0x9B, 0x7F bytes.

## Acceptance
- 12 tests pass under ASAN.
- Functional coverage of `screen.c` ≥ 75 % (from 34 %).
- `terminal_wcwidth` hit at least once in the functional suite.

## Dependencies
US-27 (the story). SEC-01 (sanitisation impl already in code).
TEST-55 stays as the narrower width-only test.
