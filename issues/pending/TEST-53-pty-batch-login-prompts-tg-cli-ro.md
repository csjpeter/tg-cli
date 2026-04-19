# TEST-53 — PTY test: tg-cli-ro rejects prompt fallback in batch mode

## Gap
`tg_cli_ro.c` always operates in batch mode; `cb_get_phone/code/
password` return -1 with a stderr message when the flag is missing.
There is no PTY test verifying that a user running
`tg-cli-ro dialogs` without `--phone` produces the documented
error line and exit code 1 (not a hang waiting for input).

A regression where `cb_get_phone` accidentally prompts could cause
CI runs to hang indefinitely.

## Scope
Add `tests/functional/pty/test_tg_cli_ro_batch_prompt_rejection.c`:
- Spawn `bin/tg-cli-ro dialogs` under PTY (no `--phone`), 3-second
  overall timeout.
- Assert the child writes `"tg-cli-ro: --phone <number> required in
  batch mode"` to stderr.
- Assert child exits with code 1 within 2 s (no hang).
- Same for `--code` missing after --phone.

## Acceptance
- Test completes within 3 s even on CI.
- If the binary blocks on stdin read, test detects the hang via
  `pty_wait_exit` timeout and fails with a clear message.

## Dependencies
- PTY-01.
