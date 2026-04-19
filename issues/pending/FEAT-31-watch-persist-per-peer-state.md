# FEAT-31 — watch should persist per-peer pts alongside global state

## Gap
`updates_state_store` persists a single `UpdatesState{pts, qts, date}`
that represents the client's global view.  This is insufficient for
channels: each channel has its own `channel_pts`.  Today, after
`watch` exits and restarts, any channel messages received while
offline would be silently missed because the client only asks for
global difference.

## Scope
1. Introduce `ChannelUpdatesState` in `src/infrastructure/
   updates_state_store.{h,c}` — a small map (64-channel cap) of
   `{channel_id → pts}`.
2. On each `getDifference` reply that includes channel updates,
   update the corresponding entry.
3. On `watch` startup, for each known channel, call
   `updates.getChannelDifference` once before entering the poll loop.
4. FT coverage:
   - `test_channel_difference_restart.c` — seed state with one
     channel at pts=10; server-side pts=15; assert startup fetches
     5 missed messages.

## Acceptance
- Per-peer state survives restart.
- Cap behaviour: 65th channel → oldest evicted (LRU).
- Man page documents the `channels_state.bin` file.

## Dependencies
- TEST-43 (JSON schema docs) not a blocker but coordinate the new
  file path.
