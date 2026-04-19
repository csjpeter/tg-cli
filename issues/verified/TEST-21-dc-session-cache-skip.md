# TEST-21 — functional test: cached DC session skips handshake

## Gap
US-15 acceptance:
> Cached foreign sessions skip the full handshake on every subsequent
> request.

Currently no functional test asserts the "second cross-DC call does
not re-do the DH handshake" invariant.

## Scope
1. First `download` on DC 4 triggers DH handshake +
   `auth.exportAuthorization` / `importAuthorization` (responders
   required).
2. Persist the session via `app/session_store.c` (v2 multi-DC file).
3. Second `download` call: assert zero handshake RPCs fire
   (mt_server records CRCs of every received frame; none match
   `req_pq`, `req_DH_params`, etc.).

## Acceptance
- Test green; the cache-skip branch in
  `src/app/dc_session.c` is covered.
