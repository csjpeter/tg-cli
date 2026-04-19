# TEST-14 — functional test for `delete --revoke`

## Gap
US-13 acceptance:
> `messages.deleteMessages` with `revoke` flag.

`test_write_path.c test_delete_messages_user/channel` does not verify
that `args.revoke = 1` lands on the wire as flags.0 = 1.

## Scope
1. Pass `--revoke` through parse → domain → mock responder.
2. Responder inspects the `flags` uint and asserts bit 0 is set.
3. Second variant without the flag: assert bit 0 is clear.

## Acceptance
- Two tests green; `src/domain/write/delete.c` flag branch covered.
