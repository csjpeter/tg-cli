# SEC-01: ANSI escape injection via malicious message body in TUI history pane

## Severity
Medium (terminal hijack / screen corruption)

## Location
`src/tui/history_pane.c` → `format_row()` → `pane_put_str()` →
`screen_put_str()` → `screen_put_str_n()`.

## Problem
`format_row()` copies `e->text` verbatim into a stack buffer and passes it to
`pane_put_str()`.  `screen_put_str_n()` decodes UTF-8 codepoints and emits them
to the terminal via `fwrite` after routing through `utf8_encode()`.  Control
characters (U+001B = ESC, U+0007 = BEL, etc.) are **not stripped**: a malicious
peer can embed `\033[2J` (erase-display) or `\033[0;31m` (colour hijack) or
`\033]0;evil title\007` (terminal title spoofing) in a message body and have
the bytes propagate to the terminal output in `screen_flip()`.

`terminal_wcwidth()` returns 0 for most control characters so they are skipped
in width accounting — but ESC (U+001B) has width −1 on some platforms and 0 on
others; in both cases the `if (w <= 0) continue;` guard in `screen_put_str_n()`
skips the *width accounting* while still letting `utf8_encode()` emit the
1-byte ESC byte into the `ScreenCell::cp` field (cp is `uint32_t`), which is
then emitted unfiltered in `screen_flip()`.

## Fix direction
Sanitize codepoints < U+0020 (and U+007F) before storing them into a
`ScreenCell`.  Replace with U+FFFD or a visible placeholder (e.g. `·`).
Document the policy in `src/tui/screen.c`.

## Test
Unit test in `tests/unit/test_tui_screen.c`: call `screen_put_str` with a
string containing `\033[2J`, flush through `screen_flip()`, and assert that no
0x1B byte appears in the output written to the `FILE*` buffer.
