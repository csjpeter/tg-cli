# TEST-48 — Functional test: tg-cli send stdin with >4 KiB and NUL bytes

## Gap
`cmd_send` reads up to `sizeof(stdin_buf) - 1 = 4095` bytes from stdin
when no `<message>` is given.  Oversized input is silently truncated,
and NUL bytes in stdin would terminate the message string early.

There is no FT that:
- Pipes 10 KiB into `cmd_send` and verifies either clean truncation
  or a warning to stderr.
- Pipes a message containing `\x00` mid-string and verifies the
  truncation point is explicit (and documented).
- Pipes an empty stdin and verifies the "empty stdin" error path.

## Scope
Create `tests/functional/test_send_stdin_edge.c`:
- Case 1: stdin has 10 KiB of `A` — assert send RPC is called once,
  body length == 4095, stderr warns about truncation.
- Case 2: stdin has `"hello\x00world"` — send RPC body is `"hello"`,
  `\x00` acts as terminator (or stderr warns explicitly).
- Case 3: stdin is empty (EOF immediately) → exit code 1, stderr
  contains "empty stdin".
- Case 4: stdin has a single `\n` → stripping drops it, body is
  empty → same error as case 3.

## Acceptance
- All 4 cases pass.
- Documented behaviour matches `tg-cli(1)` man page (update the man
  page if current docs are silent on truncation).

## Dependencies
- Feature ticket if we decide to warn on truncation rather than
  silently clipping.
