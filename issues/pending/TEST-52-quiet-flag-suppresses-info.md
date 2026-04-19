# TEST-52 — Functional test: --quiet suppresses informational output

## Gap
All three binaries advertise `--quiet`.  Implementations check
`args->quiet` before each `printf("sent\n")`, "saved: …", "marked as
read", etc.  There is no FT pinning:

- `--quiet` silences the informational line.
- `--quiet` still lets error messages through on stderr.
- `--quiet --json` behaves the same as `--json` (machine output wins).

A regression where a print is added without the `!args->quiet` guard
would leak output into scripts that aggressively check empty stdout.

## Scope
Create `tests/functional/test_quiet_flag.c`:
- Capture stdout via tmpfile.
- Run `cmd_send` with `args.quiet = 1`, verify stdout is empty
  on success.
- Run same without `--quiet` — stdout contains "sent".
- Run `cmd_send` with a forced error (mock returns RpcError) — assert
  error line on stderr regardless of quiet.
- Run `cmd_send` with `--quiet --json` — stdout contains only the
  JSON object.

## Acceptance
- 4 sub-tests pass.
- Covers at least one read command and one write command.
