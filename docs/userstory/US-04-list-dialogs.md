# US-04 — List dialogs / rooms / channels

Applies to: `tg-cli-ro`, `tg-tui` (read-only).

**Status:** done — `messages.getDialogs` with user/chat join, title + @username
extraction, `access_hash` threaded for later open (P5-01, TUI-08).

## Story
As an authenticated user I want to list my chats (DMs, groups, channels) so
that I can find the peer I want to inspect.

## Scope
`messages.getDialogs` → unified list: title, peer id/username, unread count,
last message preview, type (user/chat/channel).

## UX
```
tg-cli-ro dialogs [--limit N] [--archived] [--json]
```
Plain output: one dialog per line, padded columns.
JSON output: array of objects with stable keys.

## Acceptance
- Returns up to N dialogs (default 20) from the primary DC.
- Correctly unwraps `messages.dialogsSlice` vs `messages.dialogs` vs
  `messages.dialogsNotModified`.
- Uses cache (`cache_store`) keyed by a short TTL so repeated calls are fast.
- Unit tests for parsing at least one golden-path and one empty response.

## Dependencies
P5-01 · F-01. Requires US-03 auth completed.
