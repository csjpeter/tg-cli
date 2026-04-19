# FEAT-03 — persist updates.state across invocations

## Gap
US-07 acceptance:
> On reconnect, picks up from last `pts` / `qts` (stored in
> `~/.cache/tg-cli/updates.state`).

`src/domain/read/updates.c` holds the running state in memory only.
Between invocations we re-run `updates.getState` and miss events that
arrived while the binary was not running.

## Scope
1. `infrastructure/updates_state_store.{h,c}` (new): read/write a
   small binary record — `{pts, pts_date, qts, seq, date}` — at
   `~/.cache/tg-cli/updates.state` with mode 0600.
2. `watch` / TUI poll loop:
   - on entry, load from disk; fall back to `updates.getState` only
     if the file is missing or stale (> 24 h).
   - after each successful `updates.getDifference`, write back.
3. Exponential backoff on network errors (start 5 s, cap 5 min,
   reset on first success).
4. Unit tests for the store; functional test asserting that two
   consecutive `watch` runs skip the initial `updates.getState`.

## Acceptance
- Two `tg-cli-ro watch` runs back-to-back see no duplicated events.
- `~/.cache/tg-cli/updates.state` created with mode 0600.
- Backoff observable in the functional test (responder returns
  transient error → `wait_us` mock sees increasing sleep values).
