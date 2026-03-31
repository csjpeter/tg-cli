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
