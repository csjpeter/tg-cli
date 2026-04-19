# PTY-07 — PTY test: tg-cli-ro history output sanitization on real TTY

## Gap
Same gap as PTY-06, but for the `history` subcommand of `tg-cli-ro`.
Message text is user-controlled content that may contain arbitrary
escape sequences (e.g. someone sends a spoofed message with embedded
ANSI to redraw the victim's terminal).  No PTY test proves the batch
sanitizer blocks this.

## Scope
1. Add `tests/functional/pty/test_tg_cli_ro_history_tty.c`:
   - Seed mock history with one message whose text contains
     `\x1b]0;FAKE TITLE\x07` (OSC title-set) and `\x1bc` (RIS reset).
   - Spawn `bin/tg-cli-ro history self --limit 1` under PTY.
   - Assert output contains no ESC bytes (sanitized to `.`).
2. Add sibling test for `--json`: JSON output must NOT be sanitized
   (scripts consume it), but it MUST be `json_escape_str`'d (`\u001b`
   instead of raw ESC).  Assert the JSON path contains `\u001b`
   literal (6 chars) and not raw 0x1b.

## Acceptance
- TTY text path: no raw escape bytes in output.
- JSON path: escape bytes appear as `\u001b` literals, not sanitized
  to `.`.
- Both paths unambiguously distinguishable by a grep on the output.

## Dependencies
- PTY-01.
