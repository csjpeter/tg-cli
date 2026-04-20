# TEST-80 — Functional coverage for service messages (join/leave/pin/...)

## Gap
`src/domain/read/history.c` treats any `messageService` constructor
as "complex, drop". Seventeen distinct `messageAction*` CRCs are
never parsed in the functional suite, so group histories appear
sparsely populated and `watch` output is truncated.

## Scope
Mock-server fixture `mt_server_seed_message_service(action_crc,
payload_builder)`; one builder per action in the US-29 table.

New suite `tests/functional/test_service_messages.c`:

| test | action | asserted line fragment |
|---|---|---|
| `test_chat_create` | `messageActionChatCreate` | "created group 'Name'" |
| `test_chat_add_user` | `ChatAddUser` | "added @alice" |
| `test_chat_delete_user` | `ChatDeleteUser` | "removed @alice" |
| `test_chat_joined_by_link` | `JoinedByLink` | "joined via invite link" |
| `test_chat_edit_title` | `ChatEditTitle` | "changed title to 'X'" |
| `test_chat_edit_photo` | `ChatEditPhoto` | "changed group photo" |
| `test_pin_message` | `PinMessage` | "pinned message 12345" |
| `test_history_clear` | `HistoryClear` | "history cleared" |
| `test_channel_create` | `ChannelCreate` | "created channel 'Y'" |
| `test_channel_migrate_from` | `ChannelMigrateFrom` | "migrated from …" |
| `test_chat_migrate_to` | `ChatMigrateTo` | "migrated to …" |
| `test_group_call` | `GroupCall` | "started video chat" |
| `test_group_call_scheduled` | `GroupCallScheduled` | "scheduled video chat for …" |
| `test_invite_to_group_call` | `InviteToGroupCall` | "invited to video chat" |
| `test_phone_call` | `PhoneCall` | "called (duration, reason)" |
| `test_screenshot_taken` | `ScreenshotTaken` | "took screenshot" |
| `test_custom_action` | `CustomAction` | raw `message` string |
| `test_unknown_action_labelled` | fake CRC | "[service action 0x…]" |
| `test_service_shows_in_watch` | pinned message | `watch` output contains it |

## Acceptance
- 19 tests pass under ASAN.
- Functional coverage of `history.c` ≥ 85 % (joint with TEST-79).
- `watch` loop surfaces service messages, not filtered out.

## Dependencies
US-29 (the story). TEST-79 shares the renderer refactor.
