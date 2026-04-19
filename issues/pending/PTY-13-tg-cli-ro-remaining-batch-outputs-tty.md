# PTY-13 — PTY tests: tg-cli-ro me / contacts / user-info / search / download TTY paths

## Gap
PTY-06 / PTY-07 cover `dialogs` and `history` TTY sanitization.  The
same `tty_sanitize()` code path is exercised by:

- `print_self_plain()` (`me` subcommand)
- `cmd_contacts` plain output
- `cmd_user_info` plain output
- `cmd_search` plain output
- `cmd_download` "saved: …" line

None of these have a PTY test confirming that:
1. Control bytes in server-supplied fields are replaced with `.`.
2. The output is clean under `cat -v`.
3. The same fields in `--json` mode are JSON-escaped (`\u001b`) not
   sanitized to `.`.

## Scope
Add 5 small PTY tests under `tests/functional/pty/`:
- `test_tg_cli_ro_me_tty.c` — seed `user.first_name` with `\x1b[7m`.
- `test_tg_cli_ro_contacts_tty.c` — not directly user-controlled,
  but `user_id` formatting path still covered.
- `test_tg_cli_ro_user_info_tty.c` — seed `username` with `\x7f`.
- `test_tg_cli_ro_search_tty.c` — seed message text with `\x1b]0;T\x07`.
- `test_tg_cli_ro_download_tty.c` — seed document_filename with
  `\x1bOK` (OSC) — also verifies SEC-03 sanitization pairs with
  TTY sanitization.

## Acceptance
- 5 PTY tests pass under ASAN.
- Each test also runs the `--json` variant and asserts escape form.
- Regression in `tty_sanitize` for any caller is caught.

## Dependencies
- PTY-01, PTY-06 (pattern reference), SEC-03 (for download case).
