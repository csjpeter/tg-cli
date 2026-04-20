# TEST-79 — Functional coverage for rich message metadata

## Gap
`src/domain/read/history.c` drops messages with `fwd_from`,
`via_bot_id`, `reply_to`, `via_business_bot_id`, or `saved_peer_id`
flag bits set, setting `complex=1` and returning -1. Five big
flag branches are uncovered; tests never feed messages carrying
these fields.

## Scope
Mock-server fixtures:
- `mt_server_seed_message_forward(fwd_from_peer, fwd_from_name)`
- `mt_server_seed_message_reply(reply_to_msg_id)`
- `mt_server_seed_message_via_bot(bot_user_id)`
- `mt_server_seed_message_via_business_bot(bot_user_id)`
- `mt_server_seed_message_saved_peer(saved_peer_id)`

New suite `tests/functional/test_history_rich_metadata.c`:

1. `test_forwarded_from_channel_labelled` — line starts with "[fwd
   from @channel]".
2. `test_forwarded_from_hidden_user_labelled` — uses `from_name`
   rather than `from_id`.
3. `test_reply_to_labelled_with_msg_id` — "[↩ 12345]".
4. `test_via_bot_labelled_with_username` — "[via @gif]".
5. `test_via_business_bot_labelled` — "[biz-bot @reply_bot]".
6. `test_saved_peer_id_suffix` — "(saved)" suffix in Saved
   Messages dialog.
7. `test_multiple_flags_all_rendered` — reply + via-bot on the
   same message.
8. `test_unresolvable_bot_falls_back_to_raw_id` — failed username
   lookup prints the numeric id instead of dropping the line.

## Acceptance
- 8 tests pass under ASAN.
- Functional coverage of `history.c` ≥ 85 % (combined with
  TEST-80 service-message tests below).
- Plain-text + JSON output shapes consistent with the man-page
  contract in US-28.

## Dependencies
US-28 (the story). US-06 (baseline).
