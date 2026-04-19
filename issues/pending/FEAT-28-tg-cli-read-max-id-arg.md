# FEAT-28 — tg-cli read: surface --max-id as a first-class flag

## Gap
`cmd_read` in `tg_cli.c` takes `args->msg_id` and forwards it to
`domain_mark_read` as max-id, but the arg parser binds `msg_id` to
the positional `<msg_id>` slot.  The `read` help text does advertise
`[--max-id N]` but the parser has no explicit `--max-id` flag —
users must pass the id as a bare positional arg after `<peer>`.

Inconsistency between help text and actual parser surface.

## Scope
1. Either:
   A. Add a dedicated `--max-id N` flag to `arg_parse.c` that
      populates `args.msg_id`.  Keep the positional fallback for
      backwards compat.
   B. Remove the `[--max-id N]` from the help and from the man page;
      pin the positional-only contract.

Proposed: **A** — explicit flag reads better.

2. Update `man/tg-cli.1` to show both forms.
3. TEST-49 now depends on this fix.

## Acceptance
- `tg-cli read @alice --max-id 100` parses cleanly.
- `tg-cli read @alice 100` still parses (back-compat).
- `tg-cli read @alice --max-id` (no value) is rejected.
