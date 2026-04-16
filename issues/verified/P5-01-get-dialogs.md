# messages.getDialogs (chat/channel list)

## Description
List of active conversations: private chats, groups, channels.

## Batch
`tg-cli dialogs [--json] [--limit N]`

## Output
peer_id | peer_type | name | unread_count | last_message_preview

## Estimate
~200 lines

## Dependencies
- P4-05 ✅ (verified) — constructor registry
- P3-02 (pending) — API hívásokhoz bejelentkezés szükséges
- ARCH-05 (pending, soft) — domain layer struktúrák (Dialog, User)

## Verified — 2026-04-16 (v1)
- `src/domain/read/dialogs.c` + `tg-cli-ro dialogs` emit
  DialogEntry (kind, peer_id, top_msg, unread_count).
- **v1 limitation:** parser stops after the first Dialog because
  `PeerNotifySettings` cannot be reliably skipped without a schema
  table. Tracked as **P5-07 parse v2**.
- Title enrichment (name via users/chats vectors) — tracked as **P5-08**.
