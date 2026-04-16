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
