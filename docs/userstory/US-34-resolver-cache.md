# US-34 — Username resolver cache: hits, TTL, invalidation

Applies to: all three binaries (anything calling `info @peer`,
`send @peer`, `history @peer`, etc.).

**Status:** gap — `src/domain/read/user_info.c::rcache_lookup`,
`rcache_store`, and `resolve_cache_flush` are uncovered. Every
`@peer` → numeric id resolution currently pays a round-trip to
`contacts.resolveUsername`. No functional test verifies that
repeated use of the same `@peer` hits the in-process cache instead.

## Story
As a user running a series of commands all targeting the same
channel (`history @mychan`, `search @mychan …`, `info @mychan`), I
want only the first call to pay the `contacts.resolveUsername`
round trip. The rest should resolve locally. When the cache
entry is older than the TTL, the next call refreshes it.

## Uncovered practical paths
- **Cold lookup:** `contacts.resolveUsername` fires once for a
  new `@peer`.
- **Warm lookup:** same `@peer` called again → zero RPC.
- **TTL expiry:** cache entry older than the configured TTL → RPC
  fires again, new value stored.
- **`flush` on logout:** `--logout` clears the cache.
- **Negative cache** (`USERNAME_INVALID` / `USERNAME_NOT_OCCUPIED`):
  if an unknown `@peer` resolves to an error, the error is cached
  too (shorter TTL) to stop retry storms.
- **Cache collision / eviction:** when the fixed-size cache fills,
  least-recently-used entry is evicted (if the implementation
  uses LRU — otherwise documented FIFO).

## Acceptance
- Mock server tracks resolver RPC count.
- New suite `tests/functional/test_resolver_cache.c`:
  - `test_cold_call_resolves_once`
  - `test_warm_call_skips_rpc`
  - `test_ttl_expiry_refreshes`
  - `test_logout_flushes_cache`
  - `test_negative_result_cached`
- Functional coverage of `user_info.c` ≥ 90 % (from ~88 %).
- Documented TTL value appears in the man page CREDENTIALS
  section ("peer resolutions cached for N seconds").

## Dependencies
US-09 (user-info). US-16 (session management — logout path).
