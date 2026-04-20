# US-26 — Large-account pagination: deep dialogs, deep history

Applies to: tg-cli (batch), tg-cli-ro.

**Status:** gap — `src/domain/read/dialogs.c` is 79 % functional
covered and `history.c` is 62 %. Pagination / `offset` / `max_id`
paths are where the uncovered lines sit. No existing user story
explicitly covers an account with thousands of dialogs or a channel
with 100 000+ messages.

## Story
As a user of a long-lived Telegram account (5 000+ chats, millions
of messages), I want `dialogs` and `history` to page correctly and
predictably, so scripts can walk my account without skipping or
double-counting entries.

## Uncovered practical paths
- **`dialogs --limit 20` on an account with 2 000 dialogs:** pinned
  dialogs come back first; paging beyond `offset_id` must continue
  from where the previous call stopped.
- **`dialogs --archived`:** folder_id=1 path must use the same
  pagination logic as folder_id=0 (FEAT-32 tracks `--all`, this
  covers `--archived`).
- **Multi-page history walk:** loop of 10 × `history --limit 100
  --offset N` must hit every message, no gaps at boundaries.
- **`max_id` semantics:** negative / zero / missing → documented
  boundary behaviour. FEAT-28 is the feature; this is the coverage.
- **`messages.messagesSlice` vs `messages.messages`:** small dialogs
  return the unpaginated variant; must yield the same output shape.
- **`messages.messagesNotModified`:** mid-walk the server may tell
  us "nothing new since your hash" → should be treated as empty,
  not an error.

## Acceptance
- New mock-server helper `mt_server_seed_dialog_fixture(N)` that
  synthesises an arbitrarily large dialog list.
- New functional test `tests/functional/test_deep_pagination.c`:
  - dialogs walk of 250 entries across three pages → union has
    exactly 250 unique ids.
  - history walk of 500 messages across six pages → same.
  - `messagesNotModified` mid-walk → clean termination.
- Functional coverage of `dialogs.c` ≥ 90 %, `history.c` ≥ 80 %.
- Man pages already document `--limit` / `--offset`; add a one-line
  note that paging uses `offset_id` and is stable under concurrent
  writes.

## Dependencies
US-04 (dialogs), US-06 (history). FEAT-28 (`--max-id`) and
FEAT-32 (`--all`) are sibling features.
