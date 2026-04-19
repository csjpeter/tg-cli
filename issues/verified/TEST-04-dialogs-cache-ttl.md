# TEST-04 — functional test for dialogs cache TTL

## Gap
US-04 acceptance:
> Uses cache (`cache_store`) keyed by a short TTL so repeated calls
> are fast.

There is no functional test that proves the dialogs path consults the
cache at all. Today the domain layer may not cache — inspect
`src/domain/read/dialogs.c` and either add the caching (new FEAT)
*and* this test, or remove the promise from US-04.

## Scope
1. First assert: two back-to-back `domain_get_dialogs` calls fire
   *exactly one* RPC (second call served from cache).
2. After the TTL expires (inject a mockable clock in `cache_store`,
   or accept a `now` argument), the second call triggers a fresh RPC.

## Acceptance
- `mt_server_rpc_call_count() == 1` after two consecutive calls
  within TTL.
- After advancing the clock past TTL, a third call fires a second
  RPC.

## Dependencies
May spawn a companion FEAT ticket if the current code does not cache
at all.
