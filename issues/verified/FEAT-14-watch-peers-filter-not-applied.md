# FEAT-14 — watch --peers filter: parsed but never applied

## Gap
US-07 UX specifies:
```
tg-cli-ro watch [--peers @a,@b] [--interval SEC] [--json]
```
`src/core/arg_parse.c:250-255` parses `--peers X,Y` and stores the
comma-separated list in `ArgResult.peer`. However, `src/main/tg_cli_ro.c`
`cmd_watch()` never reads `args->peer` — every new message from
`getDifference` is printed regardless of peer. The filter is silently
ignored.

## Scope
1. Parse the comma-separated peer list in `arg_parse.c` into a dedicated
   `watch_peers` field (or reuse `peer` with a clear comment) in
   `ArgResult`.
2. Before printing each new message in `cmd_watch`, resolve the peer
   identifiers once (using `domain_resolve_username` for `@name` entries
   or parsing numeric ids directly), then compare against
   `diff.new_messages[i].peer_id`. Messages not matching any listed peer
   are silently dropped.
3. Update `arg_parse.h` to document the field.
4. Functional test: register two mock responders — one returning a message
   from peer 111, one from peer 222; pass `--peers @peer111`; assert only
   the first message appears in output.

## Acceptance
- `tg-cli-ro watch --peers @chan` prints only messages from `@chan`.
- Without `--peers`, all messages are printed (existing behaviour).
- `--help` and man page already document the flag; no new documentation
  needed unless the field name changes.
