# US-10 — Search messages

Applies to: `tg-cli-ro`, `tg-tui`.

**Status:** done — `messages.searchGlobal` (no peer) and
`messages.search` (per-peer) both wired (P5-05).

## Story
As a user I want to search for a text fragment across all chats or inside
one peer so that I can find a remembered message without scrolling.

## Scope
`messages.search` (per-peer) and `messages.searchGlobal` (all chats).

## UX
```
tg-cli-ro search [<peer>] <query> [--limit N] [--json]
```

## Acceptance
- Without `<peer>` → global search; with `<peer>` → per-peer search.
- Returns up to N results (default 20).
- Zero writes.

## Dependencies
US-03 · US-04 · P5-05 · F-05.
