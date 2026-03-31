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
