# US-06 — Read channel/chat history

Applies to: `tg-cli-ro`, `tg-tui`.

**Status:** done — `messages.getHistory` for self + @peer, text + media
kind + photo_id/document_id extraction via `tl_skip_message_media_ex`
(P5-02, P5-07, P5-09).

## Story
As an authenticated user I want to scroll back through the messages of a
peer so that I can read past conversations or channel posts.

## Scope
`messages.getHistory` with peer (username, id, or channel), paged via
`--limit` and `--offset`.

## UX
```
tg-cli-ro history <peer> [--limit N] [--offset M] [--json]
```
Plain output per line: `timestamp · sender · text` (truncated sensibly).
JSON: array of objects with full fields; media references as `{file: path}`
once US-08 lands.

## Acceptance
- Supports `@username`, numeric id, and `self`.
- Handles `channelMessagesFilter` and plain `inputPeerUser` / `Chat` /
  `Channel` — driven by a small peer-resolver.
- Cache hit returns instantly; cache miss fetches then saves.
- Hard guarantee: no `messages.readHistory` call (read-only).

## Dependencies
US-03 · US-04 (for peer resolution) · P5-02 · F-03.
