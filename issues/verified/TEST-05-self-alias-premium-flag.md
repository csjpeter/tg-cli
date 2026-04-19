# TEST-05 — functional tests for `self` alias and premium flag parse

## Gap
US-05 acceptance:
> default output fields: id, username, name, phone, premium

`test_read_path.c test_get_self` does not assert the premium flag
nor exercise the `self` alias through `arg_parse`.

## Scope
1. Responder returns a `TL_user` with premium flag set.
2. Assert `domain_get_self` extracts the flag and that
   `cmd_me`-equivalent output (captured via `freopen`) contains
   `premium: yes`.
3. Second test round-trips the CLI `self` alias through arg_parse ->
   `CMD_ME` enum.

## Acceptance
- Two new tests pass; `SelfInfo.premium` bit is covered.
