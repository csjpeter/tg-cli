# Real-time update loop

## Description
Runs in the background, receiving server push messages. In interactive mode, new messages are displayed immediately.

## Approach
- Non-blocking socket read (poll/select)
- Update processing within the interactive loop
- `updates.getDifference` periodic poll as fallback

## Estimate
~200 lines

## Dependencies
- P4-06 (pending) — updates.getDifference implementáció
- P9-02 (pending) — REPL loop (interaktív megjelenítés)

## Verified — 2026-04-16 (v1, polling fallback)
- The polling fallback is in place: `tg-cli-ro watch` and the TUI
  `poll` command both use `domain_updates_state` +
  `domain_updates_difference`. New messages surface with id, date,
  direction and text (when parseable — see P5-09 for complex
  messages).
- `updates.getDifference` new_messages vector is iterated via the
  same schema-tolerant parser as history/search.

## Remaining for v2
- Non-blocking socket read + long-poll (spec approach) to lower
  latency below the 30 s fallback interval. Needs
  `poll(2)`/`select(2)` integration in the transport layer.
