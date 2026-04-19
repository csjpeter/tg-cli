# PTY-09 — PTY test: tg-tui interactive login (phone → code → 2FA)

## Gap
`tg_tui.c` wires `cb_get_phone`/`cb_get_code`/`cb_get_password` to
the `rl_readline`-backed prompts when `--phone`/`--code`/`--password`
flags are absent.  This interactive path is completely untested
because every login FT today supplies batch flags.

A regression — e.g. `tui_read_line` breaking the label formatting,
or `rl_readline` swallowing the first keystroke after a SIGWINCH —
would not be caught.

## Scope
1. Add `tests/functional/pty/test_tg_tui_interactive_login.c`:
   - Seed mock server for `auth.sendCode` + `auth.signIn` success.
   - Launch `bin/tg-tui` (NO `--phone`) under PTY.
   - Wait for master output to contain `phone (+...): `.
   - Send `+15551234567\n`.
   - Wait for `code: ` prompt.
   - Send `12345\n`.
   - Assert "Welcome" or `tg> ` prompt appears → interactive login
     successful.
   - Send `quit\n` and assert clean exit.
2. Sibling test for 2FA path: after `code:`, server returns
   `SESSION_PASSWORD_NEEDED`, tester sends the 2FA password, and
   login completes.

## Acceptance
- Both tests pass under ASAN.
- Echoing is disabled for `code` and `password` prompts (screen
  capture shows no echoed digits for the 2FA password).
  Assert via `pty_screen_row_text` — the password chars should not
  appear at the prompt row.

## Dependencies
- PTY-01, TEST-27 (credentials_load env override).
- Needs `rl_readline` password-mode (no echo) which may not yet
  exist — spin off a prerequisite FEAT ticket if missing.
