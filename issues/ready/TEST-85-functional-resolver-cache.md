# TEST-85 — Functional coverage for @username resolver cache

## Gap
`src/domain/read/user_info.c::rcache_lookup`, `rcache_store`, and
`resolve_cache_flush` are uncovered in functional tests. Every
`@peer` resolution currently re-issues `contacts.resolveUsername`
in the test harness, so there is no regression safety for the
cache's behaviour on repeated commands.

## Scope
Mock server exposes `mt_server_request_crc_count` already. New
suite `tests/functional/test_resolver_cache.c`:

1. `test_cold_call_resolves_once` — first `info @foo` increments
   `resolveUsername` count by 1.
2. `test_warm_call_skips_rpc` — second `info @foo` adds 0 new
   RPC.
3. `test_different_peer_does_not_hit_cache` — `info @bar` adds 1.
4. `test_ttl_expiry_refreshes` — monkey-patch
   `resolve_cache_flush`-style time source (or inject a seam),
   fast-forward past the TTL, next call fires again.
5. `test_logout_flushes_cache` — after `--logout`, `info @foo`
   fires RPC again.
6. `test_negative_result_cached_with_shorter_ttl` —
   USERNAME_INVALID response for `@nobody`; second call within
   the negative TTL does NOT re-fire RPC; after the short TTL it
   does.
7. `test_cache_eviction_oldest_first` — fill the cache past
   capacity, first-inserted entry is evicted.

## Acceptance
- 7 tests pass under ASAN.
- Functional coverage of `user_info.c` ≥ 90 % (from ~88 %).
- A compile-time seam allows the tests to drive the TTL clock.

## Dependencies
US-34 (the story). US-09 (user-info baseline).
