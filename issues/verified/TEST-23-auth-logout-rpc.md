# TEST-23 — functional test for `auth.logOut` RPC on --logout

## Gap
Depends on FEAT-12 landing. Once the server-side invalidation is
implemented, we need a test that asserts the call actually fires.

## Scope
1. Seed session.
2. Register responder for `auth.logOut#3e72ba19` returning
   `auth.loggedOut#c3a2835f`.
3. Run the `--logout` code path.
4. Assert:
   - responder fired exactly once,
   - `session.bin` no longer exists,
   - binary exited 0.
5. Variant: responder returns an error — assert session file still
   wiped (best-effort logout).

## Acceptance
- Two tests green; covers the new logout branch.

## Dependencies
- Blocked by FEAT-12.
