# Edit, delete, forward, reply

## API
- `messages.editMessage(peer, id, message)`
- `messages.deleteMessages(id[], revoke)`
- `messages.forwardMessages(from_peer, id[], to_peer)`
- `messages.sendMessage(peer, message, reply_to_msg_id)`

## Batch
- `tg-cli edit <peer> <msg_id> "new text"`
- `tg-cli delete <msg_id> [--revoke]`
- `tg-cli forward <from_peer> <to_peer> <msg_id>`
- `tg-cli send <peer> --reply <msg_id> "text"`

## Estimate
~200 lines

## Dependencies
- P5-01 (pending) — peer azonosítás
- P5-03 (pending) — messages.sendMessage alap (reply_to_msg_id)
- P3-02 (pending) — bejelentkezés szükséges

## Completion notes (2026-04-16)
All four operations landed under tg-domain-write:
- **edit**: src/domain/write/edit.{h,c} — domain_edit_message sets
  flags.11 (message) on messages.editMessage#48f71778 and accepts any
  Updates envelope as success. v1 is text-only; media replacement
  remains a follow-up.
- **delete**: src/domain/write/delete.{h,c} — domain_delete_messages
  dispatches on peer kind (messages.deleteMessages#e58e95d2 for
  users/chats/self, channels.deleteMessages#84c1fd4e for channels).
  Returns messages.affectedMessages on success.
- **forward**: src/domain/write/forward.{h,c} — domain_forward_messages
  builds messages.forwardMessages#c661bbc4 with a per-message random_id
  sourced from crypto_rand_bytes; accepts any Updates envelope.
- **reply**: extended send.{h,c} with domain_send_message_reply which
  emits inputReplyToMessage#22c0f6d5 inside a flags.0 messages.sendMessage.
  Old domain_send_message is a thin wrapper passing reply_to=0.

tg-cli exposes `edit`, `delete`, `forward` and `send --reply N` via
arg_parse extensions (new CMD_EDIT / CMD_DELETE / CMD_FORWARD enums,
new peer2 / revoke / reply_to fields on ArgResult). tg-cli-ro stays
read-only by construction — the new libs are not linked to it.

Tests: test_domain_edit_delete_forward.c (8 cases spanning the
happy paths, RPC error, channel dispatch, wire-CRC inspection,
reply_to embedding). test_arg_parse.c updated: send-without-message
now means "read from stdin" and returns ARG_OK (documented).

1957 unit + 131 functional tests green, valgrind clean.
