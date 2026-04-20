# US-29 — Service messages: joins, leaves, pins, video-chat events

Applies to: tg-cli, tg-cli-ro, tg-tui (history, watch, TUI history pane).

**Status:** gap — `src/domain/read/history.c` checks for
`messageService` and returns "complex=1; -1" immediately, dropping
the entire row. No constructor is parsed, no date / id is retained.
For any real group chat this means large swathes of history appear
empty.

## Story
As a user reading a group's history, I want to see what
*happened* in the chat, not just what was said. "Alice joined the
group", "Bob pinned a message", "video chat started" are first-class
chat events; scripts that track member turnover or moderation
depend on them.

## Covered message actions (priority subset)
| action CRC | display |
|---|---|
| `messageActionChatCreate` | "created group 'Name'" |
| `messageActionChatAddUser` | "added @alice" |
| `messageActionChatDeleteUser` | "removed @alice" |
| `messageActionChatJoinedByLink` | "joined via invite link" |
| `messageActionChatEditTitle` | "changed title to 'New name'" |
| `messageActionChatEditPhoto` | "changed group photo" |
| `messageActionPinMessage` | "pinned message <id>" |
| `messageActionHistoryClear` | "history cleared" |
| `messageActionChannelCreate` | "created channel 'Name'" |
| `messageActionChannelMigrateFrom` | "migrated from group <id>" |
| `messageActionChatMigrateTo` | "migrated to channel <id>" |
| `messageActionGroupCall` | "started video chat" |
| `messageActionGroupCallScheduled` | "scheduled video chat for <ts>" |
| `messageActionInviteToGroupCall` | "invited to video chat" |
| `messageActionPhoneCall` | "called (duration Ms, <reason>)" |
| `messageActionScreenshotTaken` | "took screenshot" |
| `messageActionCustomAction` | raw `message` string |

All others → "[service action <crc_hex>]".

## Uncovered practical paths
- `messageService` parsing + render as above.
- Forward compatibility: unknown action CRC labelled safely
  (US-24 shares this path).
- `tg-cli-ro watch` surfaces service actions as individual update
  entries, not dropped.

## Acceptance
- New suite `tests/functional/test_service_messages.c` — one test
  per row of the table above, seeded into mock history.
- Functional coverage of `history.c` (service branch) reaches
  ≥ 85 % (combined with US-28).
- `watch` prints the service actions on the poll loop.
- Man page `history` table lists which actions are translated.

## Dependencies
US-06 (baseline). US-07 (watch). US-28 (shared renderer
refactor).
