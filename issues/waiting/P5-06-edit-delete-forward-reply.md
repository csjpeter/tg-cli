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
P5-01, P5-03
