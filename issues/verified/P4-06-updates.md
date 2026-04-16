# updates.getState / updates.getDifference

## Description
Real-time update handling. After first login, `updates.getState` returns the current state, then `updates.getDifference` is used to poll for new events.

## API
- `updates.getState` → pts, qts, date, seq
- `updates.getDifference(pts, qts, date)` → new messages, read statuses, etc.

## Estimate
~200 lines

## Dependencies
- P4-05 ✅ (verified) — constructor registry a válasz parszoláshoz
- P3-02 (pending) — updates.getState bejelentkezést igényel

## Verified — 2026-04-16 (v1)
- `src/domain/read/updates.c` — `domain_updates_state()` +
  `domain_updates_difference()` handle
  differenceEmpty / differenceTooLong / difference(+Slice).
- `tg-cli-ro watch` 30 s poll loop with SIGINT handler.
- **v1 limitation:** reports only new_messages count; walking the
  message bodies needs **P5-07** + **P5-09**.
