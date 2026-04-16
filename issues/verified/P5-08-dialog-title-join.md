# Dialog title enrichment via users[] / chats[] join

## Description
`messages.getDialogs` returns a Dialog vector **plus** a Users vector and
a Chats vector in the same frame. The Dialog carries only a Peer (id);
human-readable titles live in the User / Chat / Channel entries. Our v1
parser emits peer_id only, so `tg-cli-ro dialogs` shows numeric ids
instead of "Mom", "@work_channel", etc.

## Goal
After P5-07 (parse v2) lands, walk the trailing `chats:Vector<Chat>` and
`users:Vector<User>` vectors and populate a `title` / `username` on each
returned `DialogEntry` by matching on id.

## Steps
1. Extend `DialogEntry` with `char title[128]` and `char username[64]`.
2. After iterating `dialogs:Vector<Dialog>`, iterate `chats:Vector<Chat>`
   and keep (id, title) pairs; same for users.
3. For each DialogEntry, look up its peer_id in the corresponding pool.
4. Plain and JSON output gains the new columns.

## Estimate
~250 lines incl. tests.

## Dependencies
P5-07 (needs multi-entry vector walk) · `tl_skip` helpers for the
trailing User / Chat / Channel objects.

## Blocked on — 2026-04-16
Requires further tl_skip work before the users/chats vectors in the
`messages.dialogs` response can be reached:
- `tl_skip_message` (advance a whole Message without parsing) — needed
  because messages:Vector<Message> sits between dialogs and chats.
- Chat / channel / chatForbidden / channelForbidden / chatEmpty
  skippers — need ChatPhoto skipper (chatPhotoEmpty + chatPhoto).
- User / userEmpty skippers — need UserProfilePhoto + UserStatus
  skippers, plus 20+ flag-conditional optional fields.

Estimated ~400 LoC of skippers before a join can land. Parked until
phase 3 of P5-07 (Chat/User/Media skippers) completes.

## Verified — 2026-04-16
- `src/core/tl_skip.{h,c}`: added `tl_skip_message` (required to walk
  the messages vector between dialogs and chats in the response).
- `src/domain/read/dialogs.{h,c}`:
  - `DialogEntry` gains `title[128]` + `username[64]`.
  - After the dialogs vector the parser now iterates the messages
    vector (via tl_skip_message), collects chats via
    tl_extract_chat and users via tl_extract_user into temporary
    arrays, then back-fills title/username on each DialogEntry by
    matching peer_id.
  - Graceful degrade: any failure mid-join leaves titles empty and
    returns the dialog list that was already populated.
- `src/main/tg_cli_ro.c` + `src/main/tg_tui.c`: `dialogs` output
  now shows title and @username (plain + JSON).
- `tests/unit/test_domain_dialogs.c::test_dialogs_title_join_user`
  validates the join end-to-end: a user with first_name=Alice,
  last_name=Smith, username=alice_s surfaces in the DialogEntry
  as title="Alice Smith", username="alice_s".

Tests: 1882 -> 1887. Valgrind: 0 leaks. Zero warnings.
