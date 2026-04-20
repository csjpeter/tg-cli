# TEST-87 — PTY test: password prompts mask input and zero buffer on abort

## Gap
`src/platform/posix/terminal.c::terminal_read_password` and its
cleanup logic are uncovered in functional tests. PTY-09 is
scoped to SMS-code entry, not masked prompts. The config wizard's
api_hash prompt (FEAT-37) relies on this function, as does the
tg-tui 2FA password step.

## Scope
New PTY test `tests/functional/pty/test_password_prompt.c` using
the libptytest harness:

1. `test_password_prompt_hides_typed_chars` — spawn tg-tui login,
   type "hunter2\n" at the 2FA prompt; the pty output stream
   echoes whitespace / nothing, never the literal characters.
2. `test_password_prompt_restores_echo_on_success` — after the
   prompt returns, a subsequent ordinary prompt echoes normally.
3. `test_password_prompt_restores_echo_on_sigint` — Ctrl-C during
   input kills the process; a wrapper starts a fresh one and sees
   correct echoing behaviour.
4. `test_password_prompt_backspace_edits_hidden_buffer` — type
   "bad\b\b\bhunter2", the resulting password is "hunter2" (not
   "badhunter2").
5. `test_password_prompt_ctrl_d_on_empty_reports_error` — plain
   Ctrl-D: the caller gets an empty string and exits 1.
6. `test_password_prompt_long_input_fits_buffer` — 256-char
   password accepted without truncation.

## Acceptance
- 6 PTY tests pass under ASAN.
- Functional coverage of `terminal.c` ≥ 50 % (from 1.6 %).
- Harness reuse: new helpers added to `tests/functional/pty/`
  common code.

## Dependencies
US-36 (the story). FEAT-37 config wizard (for the api_hash
variant). PTY-09 (companion: the code prompt).
