# US-28 — Rich message metadata: forwarded, via-bot, reply-to, business bots

Applies to: tg-cli, tg-cli-ro, tg-tui (history, search output).

**Status:** gap — `src/domain/read/history.c` has branches for
`fwd_from` (flags bit 2), `via_bot_id` (bit 11), `reply_to` (bit 3),
`via_business_bot_id` (flags2 bit 0), `saved_peer_id` (bit 28). The
code paths fall back to "complex=1; return -1" and drop the row
without extracting any metadata. No user story documents what the
printed output should look like when these bits are set.

## Story
As a user reading chat history from a real Telegram group, I want
to see *who* forwarded a message from *where*, whether a message
was written via a bot (e.g. via @gif), and what message a reply was
attached to. Without this context modern group chats are
unreadable — most messages are replies or forwards.

## Desired output
Plain text (`tg-cli`, `tg-cli-ro history`):
```
  12345  2026-04-20 14:22:07  alice: Hey everyone
  12346  2026-04-20 14:22:39  bob [↩ 12345]: yes
  12347  2026-04-20 14:23:15  carol [fwd from @channel]: fresh news
  12348  2026-04-20 14:23:52  dave [via @gif]: <gif>
  12349  2026-04-20 14:24:10  eve [biz-bot @reply_bot]: auto reply
```

TUI history pane: same prefix tokens inside the "from" column,
truncated with an ellipsis when the pane is narrow.

## Uncovered practical paths
- `fwd_from` set → read `messageFwdHeader`, extract `from_name`
  or `from_id` (peer), print "[fwd from <who>]".
- `via_bot_id` set → read long, resolve to @username, print
  "[via @bot]".
- `reply_to` set → read `messageReplyHeader`, extract `reply_to_msg_id`,
  print "[↩ <msg_id>]".
- `via_business_bot_id` set → long, resolve, print
  "[biz-bot @user]".
- `saved_peer_id` set (layer 185+) → print "(saved)" suffix for
  messages in the Saved Messages dialog organised by topic.
- Graceful degradation: if the referenced bot/peer cannot be
  resolved, print the raw id rather than dropping the whole line.

## Acceptance
- New suite `tests/functional/test_history_rich_metadata.c`:
  - five mock fixtures (one per flag bit above).
  - stdout / pane contains the expected token prefix.
  - plain text + JSON output shapes documented in man page tables.
- Functional coverage of `history.c` ≥ 85 % (from 62 %).
- Man page `history` sections in tg-cli.1, tg-cli-ro.1 list the
  new prefix tokens.

## Dependencies
US-06 (history baseline). US-28's test fixtures unblock US-29
(service messages) since both extend the history renderer.
