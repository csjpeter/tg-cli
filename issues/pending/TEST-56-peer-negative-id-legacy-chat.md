# TEST-56 — Functional test: negative peer id parsing (legacy group chats)

## Gap
Telegram legacy group chats use negative peer ids (e.g. -100123456789)
and "mega-groups"/channels use very negative ids shifted by
`-1000000000000`.  `watch_peers_resolve()` accepts negatives via
`strtoll`, but there is no FT verifying that:

- `--peers -100123456` is parsed as a plain negative id.
- `--peers -1001234567890` (super-group range) is parsed correctly
  and not confused with a flag (the leading `-` could be mistaken
  for argument start).
- `resolve_history_peer` handles negative numerics consistently
  with positive ones.

## Scope
Create `tests/functional/test_peer_negative_id.c`:
- Case 1: call `watch_peers_resolve(..., "-100,20,-9999,self", ...)`.
  Assert `ids[0] == -100`, `ids[1] == 20`, `ids[2] == -9999`,
  `ids[3] == 0`, returned count == 4.
- Case 2: `arg_parse(["tg-cli-ro", "watch", "--peers", "-100200"])`
  returns ARG_OK with `args.watch_peers = "-100200"`.
- Case 3: `arg_parse(["tg-cli-ro", "history", "-100200"])` (peer
  arg positional) works — a bare negative number is accepted as
  peer.

## Acceptance
- Negative ids in both flag-value and positional slots parse
  correctly.
- No false ARG_ERROR because of the leading `-`.
