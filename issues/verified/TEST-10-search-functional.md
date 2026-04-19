# TEST-10 — functional tests for search (global + per-peer)

## Gap
US-10 is completely absent from the functional suite. `messages.search`
and `messages.searchGlobal` have zero end-to-end coverage.

## Scope
Three new cases in `tests/functional/test_read_path.c` (or a new
`test_search.c` — your call):
1. `search_global_happy` — responder matches `messages.searchGlobal
   #4bc6589a` and returns three messages; domain layer emits three
   entries.
2. `search_per_peer_happy` — responder matches `messages.search
   #a7b4e929` and returns two peer-scoped messages.
3. `search_limit_respected` — asserts the `limit` field on the wire
   equals what we passed (gated on FEAT-08).

## Acceptance
- Three tests green; `src/domain/read/search.c` hits both code paths.
