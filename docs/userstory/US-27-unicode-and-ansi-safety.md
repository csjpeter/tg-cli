# US-27 — Unicode text rendering + ANSI escape injection safety

Applies to: all three binaries (plain-text output for tg-cli and
tg-cli-ro; live pane rendering for tg-tui).

**Status:** gap — `src/tui/screen.c` `utf8_decode` is uncovered,
`terminal_wcwidth` is uncovered, and the SEC-01 control-character
sanitisation block in `screen.c` L77–92 has no functional test. The
existing TEST-55 (emoji width) ticket covers dialog titles only. No
coverage exists for message *bodies* with multi-byte sequences,
malformed UTF-8, or terminal-escape injection.

## Story
As a user reading messages, I want:
1. Every well-formed UTF-8 codepoint (emoji, CJK, Hebrew, Arabic,
   combining mark) to render at the correct column width and not
   corrupt the surrounding layout.
2. Malformed UTF-8 to be replaced with U+FFFD REPLACEMENT CHARACTER
   without aborting parsing or dropping the rest of the message.
3. Any control character a malicious sender puts into the message
   body (ANSI CSI, OSC, DEC, C1 controls, ESC, BEL, DEL) to be
   neutralised so it cannot hijack my terminal — no title change,
   no colour-reset, no cursor movement, no bell spam.

## Uncovered practical paths
- **ANSI injection in plain-text output:** tg-cli-ro `history`
  should print a text body containing `\x1b[2J` (clear screen) as
  literal middle-dot characters, never act on the escape.
- **TUI pane rendering:** tg-tui `history` displaying a message
  body with emoji, CJK, and a right-to-left (RTL) character must
  land on the correct pane column.
- **Overlong UTF-8 sequences:** server-supplied 5-byte or 6-byte
  (non-standard) UTF-8 must be treated as U+FFFD.
- **Lone surrogate in UTF-8:** U+D800..U+DFFF encoded as 3-byte
  UTF-8 must be sanitised.
- **Zero-width joiner sequences** (e.g. 👨‍👩‍👧) display as a single
  cluster, not as overflowing glyphs.
- **UTF-8 BOM in message body** (U+FEFF) is treated as zero-width,
  not printed as "" garbled.

## Acceptance
- New suite `tests/functional/test_text_rendering_safety.c`:
  - tg-cli-ro and tg-cli: feed `history` a fixture message containing
    each hazardous sequence above, assert stdout contains no raw
    `\x1b`, `\x07`, `\x9b` byte, and the message is preserved
    verbatim minus the neutralised escapes.
  - tg-tui: the screen buffer after rendering the hazardous message
    contains exactly the expected cells (no escape fragment).
  - Malformed UTF-8 replacement round-trip preserves the adjacent
    valid codepoints.
- Functional coverage of `screen.c` ≥ 75 % (from 34 %).
- Man pages §SECURITY for all three binaries gain a one-paragraph
  note on control-char neutralisation.

## Dependencies
SEC-01 (the sanitisation implementation already in code). Existing
TEST-55 remains the narrower emoji-width variant.
