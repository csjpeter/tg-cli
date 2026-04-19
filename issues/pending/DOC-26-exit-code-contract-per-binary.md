# DOC-26 — Exit code contract per binary + cross-reference from help

## Gap
`man/tg-cli-ro.1` has an EXIT STATUS section but the table is
inconsistent:

- `0` — success (clear)
- `1` — usage error or runtime failure (mixed: different root cause,
  same code)
- `2` — command not available in this binary (e.g. `tg-cli-ro send`
  returns 2)

Same binary uses `2` for "subcommand not in read-only set" but
elsewhere the binary also returns 1 for "send-file required args
missing" — mixing usage (1) and "wrong binary" (2) rules.

`tg-cli.1` and `tg-tui.1` have no exit code table at all (tg-tui has
one but doesn't distinguish interactive exit paths).

## Scope
1. Define a single table in `docs/dev/exit-codes.md`:
   ```
   0  ok
   1  general runtime failure
   2  usage error (bad argv / missing required flag)
   3  authentication failed
   4  network/connection failure
   5  server returned RPC error
   ```
2. Update all three binaries to use the table consistently
   (`fprintf(stderr, "...required\n"); return 2;` instead of `1`).
3. Update `man/*.1` EXIT STATUS sections to match.
4. Add `--help` footer: "Exit codes: see man page EXIT STATUS."

## Acceptance
- Every `return N;` in the `cmd_*` functions uses the documented
  table.
- Man pages match the code (grep check possible).
- No user-visible exit-code change for backwards compatibility with
  scripts that relied on "1 = any error" — document the change as
  breaking and bump version accordingly.

## Dependencies
- Breaking change: call out in CHANGELOG / README.
