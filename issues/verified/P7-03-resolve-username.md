# contacts.resolveUsername

## Description
@username → peer_id resolution. Implicitly used in all commands when receiving an argument starting with @.

## Estimate
~80 lines

## Dependencies
- P3-02 (pending) — API híváshoz bejelentkezés szükséges
- P4-05 ✅ (verified) — constructor registry a válasz parszoláshoz

## Verified — 2026-04-16 (v1)
- Subsumed into `domain_resolve_username()` inside
  `src/domain/read/user_info.c`; reused by history/search for
  @username peer resolution.
