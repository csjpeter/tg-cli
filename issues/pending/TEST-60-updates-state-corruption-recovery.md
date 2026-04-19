# TEST-60 — Functional test: watch recovers from corrupt updates_state.bin

## Gap
`updates_state_load` reads a small binary file.  There is no FT
verifying behaviour when the file is:

1. Truncated mid-struct (first save interrupted).
2. Byte-swapped (cross-arch file from another machine).
3. Magic bytes corrupted.
4. Size correct but fields nonsensical (pts = INT32_MAX, date = -1).

Current behaviour per source: `updates_state_load` returns non-zero
and watch falls back to `domain_updates_state` (fresh fetch from
server).  That's the correct contract, but no FT pins it; a
regression that auto-restored a corrupt file would silently poison
subsequent saves.

## Scope
Create `tests/functional/test_updates_state_recovery.c`:
- For each of the 4 corruption modes above, write the file with the
  matching bytes, then call `updates_state_load`.
- Assert rc != 0 and the in-memory state is zeroed (never partial).
- For the "truncated mid-save" case, verify the corrupt file is
  eventually overwritten by a fresh save (not left poisoning future
  loads).

## Acceptance
- 4 corruption modes each produce clean fallback.
- No crash / UB reported by ASAN.
- `watch` startup latency after a corrupt file is ≤ fresh-login
  latency (no retry storm).
