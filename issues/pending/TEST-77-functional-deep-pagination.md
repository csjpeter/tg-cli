# TEST-77 — Functional coverage for deep pagination (dialogs + history)

## Gap
`src/domain/read/dialogs.c` is 79 % functionally covered and
`history.c` is 62 %. Multi-page walks, `messagesSlice` variant,
`messagesNotModified` mid-walk, and `--archived` pagination are
untested.

## Scope
Mock-server extension:
- `mt_server_seed_dialog_fixture(int count)` — synthesises an
  arbitrary number of mock dialogs with stable ids.
- `mt_server_seed_history_fixture(int peer, int count)` — same for
  messages.
- `mt_server_expect_messages_not_modified(after_msg_id)` — one-shot
  sentinel mid-walk.

New suite `tests/functional/test_deep_pagination.c`:

1. `test_dialogs_walk_250_entries_across_pages` — three calls of
   `dialogs --limit 100`; union has 250 unique ids, no duplicates.
2. `test_dialogs_archived_walk` — same but folder_id=1.
3. `test_history_walk_500_messages_across_pages` — six calls of
   `history --limit 100 --offset N`; 500 unique msg_ids, strict
   monotonic order.
4. `test_history_messages_not_modified_mid_walk` — server replies
   `messagesNotModified` on page 3; walk terminates cleanly,
   pages 1-2 output is preserved, no error exit.
5. `test_dialogs_messages_slice_vs_messages` — small fixture
   returns unpaginated `messages.messages`; output shape matches
   the `messagesSlice` variant.

## Acceptance
- 5 tests pass under ASAN.
- Functional coverage of `dialogs.c` ≥ 90 %, `history.c` ≥ 80 %.

## Dependencies
US-26 (the story). US-04, US-06. Cross-link with FEAT-28
(`--max-id`) and FEAT-32 (`--all`).
