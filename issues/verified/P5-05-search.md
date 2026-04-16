# messages.search (message search)

## Batch
`tg-cli search <peer> "keyword" [--limit N] [--json]`

## Estimate
~100 lines

## Dependencies
- P5-01 (pending) — peer azonosítás
- P3-02 (pending) — bejelentkezés szükséges

## Verified — 2026-04-16 (v1)
- `src/domain/read/search.c` handles messages.search (peer) and
  messages.searchGlobal.
- `tg-cli-ro search [<peer>] <query>`.
- Same single-entry v1 limitation as P5-01 — tracked by **P5-07**.
