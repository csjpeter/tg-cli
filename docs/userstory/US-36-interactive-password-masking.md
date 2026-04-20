# US-36 — Interactive password prompts with proper terminal masking

Applies to: tg-tui (REPL + curses mode), tg-cli login wizard.

**Status:** gap — `src/platform/posix/terminal.c::terminal_read_password`
is uncovered in functional tests. `src/app/config_wizard.c` uses it
for the `api_hash` prompt. The interactive 2FA prompt path in
`src/app/auth_flow.c` also relies on it. PTY-09 targets interactive
login but only covers the SMS-code prompt — not the 2FA / api_hash
masked input.

## Story
As a user typing secrets (my 2FA password, my Telegram api_hash)
into a tg-tui or tg-cli wizard prompt, I want the characters I
type to NOT appear on screen and NOT appear in my scrollback.
Backspace must still work. A paste must not be echoed either.

## Uncovered practical paths
- **Echo off:** `termios` has `ECHO` cleared while the prompt is
  active.
- **Echo restored on return:** whether the user presses Enter,
  Ctrl-C, or Ctrl-D — `TCSAFLUSH` restores the previous `c_lflag`.
- **Backspace** and **Ctrl-U** (erase line) work during entry;
  the "masked" state is not a blob display — it is a blank line.
- **Ctrl-C** during masked input raises SIGINT; the password
  buffer is zeroed (defence against swap leakage).
- **Ctrl-D / EOF** on empty line aborts the prompt with an error.
- **Pipe / non-TTY stdin:** prompt short-circuits and exits with
  the documented batch-mode hint (already implemented; needs a
  functional assertion beyond the current config_wizard pty test).
- **Long input (1 KB):** fits inside the bounded buffer; no
  truncation without warning.

## Acceptance
- New PTY test `tests/functional/pty/test_password_prompt.c`:
  - asserts the echoed bytes after "pw:" prompt are NOT the typed
    characters.
  - asserts backspace clears the in-memory buffer (next Enter
    submits the corrected string).
  - asserts SIGINT during prompt aborts without leaking.
  - asserts Ctrl-D on empty line returns empty string → caller
    reports error.
  - non-TTY stdin path already tested in TEST-25; cross-reference.
- Functional coverage of `terminal.c` ≥ 50 % (from ~2 %).
- Man page CONFIGURATION and LOGIN sections note that api_hash
  and 2FA password prompts mask input.

## Dependencies
FEAT-37 wizard (shipped). US-03 login flow. PTY-09 (interactive
login) — this ticket extends its reach to masked prompts.
