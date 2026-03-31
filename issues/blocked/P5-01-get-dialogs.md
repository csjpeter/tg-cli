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
