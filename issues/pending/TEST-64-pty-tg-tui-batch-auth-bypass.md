# TEST-64 — PTY test: tg-tui batch flags skip interactive prompts

## Gap
FEAT-15 (verified) wired `--phone`/`--code`/`--password` to `tg-tui`,
but no PTY test confirms that when all three are supplied the TUI
never prints a prompt.  A regression in `cb_get_phone` that calls
`tui_read_line` even when `c->phone` is non-NULL would appear to work
on a non-PTY stub (prompts go to stdout silently) but fail visibly
on real terminals.

## Scope
Add `tests/functional/pty/test_tg_tui_batch_no_prompts.c`:
- Spawn `bin/tg-tui --phone +15551234 --code 12345` under PTY.
- Mock server satisfies the auth.sendCode / auth.signIn chain.
- Assert the master NEVER shows the strings "phone (+..):", "code:",
  or "2FA password:".
- Assert the REPL prompt `tg> ` appears within 5 s.
- Send `quit` → clean exit.

## Acceptance
- Batch flags supersede prompts on a real PTY.
- Test does not hang (strict 8 s timeout).

## Dependencies
- PTY-01, FEAT-15 (verified).
