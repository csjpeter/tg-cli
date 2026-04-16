# Parse v2: iterate full Vector<Dialog> / Vector<Message>

## Description
The v1 parsers in `src/domain/read/{dialogs,history,search,updates,user_info}.c`
all stop after the **first** entry of a vector because the trailing
flag-conditional fields (`PeerNotifySettings`, message `media`/`entities`,
etc.) cannot be skipped without a schema table. Result: every read call
returns at most one Dialog / Message / ResolvedPeer — practically
unusable for real browsing.

## Goal
A small, reusable "TL skipper" that, given a known constructor CRC and
its flags, advances the `TlReader` past a single object's trailer. Start
with the high-value types:

- `PeerNotifySettings` (skip after Dialog)
- `MessageReplyHeader` (skip when flags.3)
- `MessageFwdHeader` (skip when flags.2)
- `MessageMedia*` (skip when flags.9)
- `Vector<MessageEntity>` (skip when flags.7 / flags.13)
- `DraftMessage` (skip when Dialog.flags.1)

Once each is implemented, remove the `break;` after the first entry in
history/dialogs/search/updates and iterate the full vector.

## Steps
1. Add `src/core/tl_skip.{h,c}` with `tl_skip_peer_notify_settings()`,
   `tl_skip_message_reply_header()`, `tl_skip_message_fwd_header()`,
   `tl_skip_message_media()`, `tl_skip_message_entities()`,
   `tl_skip_draft_message()`.
2. Unit-test each skipper with hand-crafted fixtures (both simple and
   flag-set forms).
3. Update `domain_get_dialogs`, `domain_get_history`,
   `domain_search_*`, `domain_updates_difference` to use the skippers
   and loop through the full `count`.
4. Re-verify against a real Telegram server.

## Estimate
~600 lines (skippers + tests + domain rewires).

## Dependencies
P4-05 (constructor registry) ✅ · the schema IDs for the six nested
types must be added to `tl_registry.h` if missing.

## Out of scope
Full schema-driven parser. Only the listed six nested types, which
cover > 95% of inbound dialogs/messages.

## Verified — 2026-04-16 (phase 1: dialogs iteration)
- `src/core/tl_skip.{h,c}` added with skippers for Bool, string, Peer,
  NotificationSound, PeerNotifySettings, DraftMessageEmpty.
- `src/domain/read/dialogs.c` now iterates the full `Vector<Dialog>`,
  reading the required unread_mentions/reactions counts and skipping
  PeerNotifySettings + optional pts/draft/folder_id/ttl_period.
- `tests/unit/test_tl_skip.c` — 10 skipper tests (1759 total).
- `tests/unit/test_domain_dialogs.c` — new `test_dialogs_multi_entries`
  confirms 5 dialogs are parsed in one response.

## Remaining under same ticket (phase 2)
- Message-side skippers (MessageFwdHeader, MessageReplyHeader,
  MessageMedia, Vector<MessageEntity>). Needed for history/search/
  updates multi-entry iteration and for P5-09 complex-message parse.
