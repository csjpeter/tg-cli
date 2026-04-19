# FEAT-09 — per-peer search in tg-tui REPL

## Gap
US-10 acceptance:
> Without `<peer>` → global search; with `<peer>` → per-peer search.

`src/main/tg_tui.c do_search` hard-codes `domain_search_global`, so the
REPL cannot scope a search to one peer even though the domain layer
exposes `domain_search_peer`.

## Scope
1. Extend REPL `search` parser to accept `search <peer> <query>` (first
   token starting with `@` or matching `self`/numeric id treated as
   peer).
2. Dispatch to `domain_search_peer` when a peer was given;
   `domain_search_global` otherwise.
3. Update `print_help` + `man/tg-tui.1` (see DOC-03).

## Acceptance
- `tg> search @peer hello` runs a `messages.search` scoped to `@peer`.
- `tg> search hello` unchanged (global).
