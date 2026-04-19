# TEST-06 — history: @username / numeric id / self / channel access_hash / --offset / cache

## Gap
US-06 acceptance:
> Supports `@username`, numeric id, and `self`.
> Handles `channelMessagesFilter` and plain `inputPeerUser` / `Chat` /
> `Channel` — driven by a small peer-resolver.
> Cache hit returns instantly; cache miss fetches then saves.

`test_history_empty` + `test_history_one_message_empty` only cover a
single peer path. Missing: `@username` resolution, numeric id,
`self` (Saved Messages), channel `access_hash` threading, `--offset`,
cache-hit.

## Scope
Six new cases under `tests/functional/test_read_path.c`:
1. `history_self` — peer=self hits `messages.getHistory` with
   `inputPeerSelf`.
2. `history_user_numeric_id` — peer=`123` hits `inputPeerUser(id=123,
   access_hash=0)`.
3. `history_username_resolve` — peer=`@foo` triggers
   `contacts.resolveUsername` then `getHistory`.
4. `history_channel_access_hash` — peer resolved via dialogs; second
   `getHistory` call carries the access_hash.
5. `history_offset_flag` — `args.offset = 50` lands on the wire.
6. `history_cache_hit` — two consecutive calls within TTL fire one
   RPC (mirrors TEST-04 for history).

## Acceptance
- Six tests green; `src/domain/read/history.c` branch coverage rises.
