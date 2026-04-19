# TEST-54 — PTY test: tg-cli send stdin routing is TTY-aware

## Gap
`cmd_send` in `tg_cli.c` reads from stdin only when `isatty(0)` is
false (i.e. stdin is piped/redirected).  When stdin is a terminal,
it prints an error demanding the message as argv.

`test_send_stdin.c` (FT) covers the pipe path only.  There is no
PTY test confirming that when stdin is attached to a terminal and
the user forgot the message, the binary fails fast instead of
blocking on `fread`.

## Scope
Add `tests/functional/pty/test_tg_cli_send_stdin_tty.c`:
- Launch `bin/tg-cli send @user` under PTY (stdin AND stdout on PTY).
- Assert child writes `"<message> required (or pipe it on stdin)"`
  to stderr within 1 s.
- Assert exit code 1 within 2 s (no hang).

Sibling test: stdin is a pipe (so `isatty(0)` false) but pipe is
closed with zero bytes → EOF → same "empty stdin" error.

## Acceptance
- Both cases terminate under 2 s.
- Regression caught if `isatty(0)` branch is ever broken.

## Dependencies
- PTY-01.
