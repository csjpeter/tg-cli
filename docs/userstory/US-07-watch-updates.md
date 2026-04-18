# US-07 — Watch incoming messages

Applies to: `tg-cli-ro`, `tg-tui`.

**Status:** done — `updates.getState` seed + `updates.getDifference` poll
loop in tg-cli-ro `watch` and TUI idle-poll (TUI-10).

## Story
As an authenticated user I want to run a command that shows me new messages
as they arrive so that I can monitor a channel or feed without opening an
official client.

## Scope
Polling loop over `updates.getDifference` (seeded by `updates.getState`).
MTProto push (long-poll with `ping_delay_disconnect` + `updates.channelDifference`)
is an optimisation for a later iteration.

## UX
```
tg-cli-ro watch [--peers @a,@b] [--interval SEC] [--json]
```
Streams one line per incoming message to stdout. Exits on SIGINT.

## Acceptance
- Default interval 30 s; configurable down to 2 s (API rate limits).
- Survives transient network errors with exponential backoff.
- On reconnect, picks up from last `pts` / `qts` (stored in
  `~/.cache/tg-cli/updates.state`).
- tg-cli-ro prints plain lines or JSON objects; tg-tui consumes the same
  domain function and redraws the relevant pane.

## Dependencies
US-03 · US-04 · P4-06 · P9-03 · F-04.
