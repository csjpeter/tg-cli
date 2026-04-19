# TEST-45 — Functional test: watch --peers filter actually gates output

## Gap
`cmd_watch` in `tg_cli_ro.c` resolves `--peers X,Y,Z` up front and then
filters `diff.new_messages[i].peer_id` via `watch_peer_allowed`.  The
resolver and allowance function are pure logic but no FT drives a full
watch cycle with a non-empty diff to confirm:

1. Messages from allowed peers appear in output.
2. Messages from disallowed peers are dropped.
3. "self" token matches `peer_id == 0`.
4. Numeric id tokens match exactly.
5. `@username` tokens are resolved via the mock server.

FEAT-14 noted the filter existed but was unapplied; that fix landed,
yet only one tiny test covers the allowance helper.  A regression in
the `strtok`/resolve loop is currently invisible.

## Scope
Create `tests/functional/test_watch_peers_filter.c`:
- Mock responds to `updates.getDifference` with 4 messages:
  peer_ids = {10, 20, 0, 30}.
- Also arm mock to resolve `@alice` → id 10 and `@bob` → id 99
  (unrelated).
- Case 1: `--peers @alice,self,20` → output has 3 messages
  (10, 20, 0) and not 30.
- Case 2: `--peers ""` or no flag → all 4 appear.
- Case 3: `--peers @nonexistent` → exit non-zero, error to stderr.
- Case 4: `--peers` list of 65 tokens → only first 64 stored
  (`WATCH_PEERS_MAX`).

## Acceptance
- All 4 cases pass under ASAN.
- Test runs in under 1 s (one poll tick per case).

## Dependencies
- None — the mock can inject a ready difference before watch's
  first poll.
