# DOC-08 — US-07 promises an updates.state file that does not exist

## Gap
`docs/userstory/US-07-watch-updates.md` line 27 says:
> On reconnect, picks up from last `pts` / `qts` (stored in
> `~/.cache/tg-cli/updates.state`).

The current `watch` implementation (`src/domain/read/updates.c`) holds
pts/qts in memory only — nothing writes or reads a `updates.state`
file. Either we implement the persistence (see FEAT-03) or we soften
the user-story acceptance to match reality.

## Scope
Option A (preferred if FEAT-03 lands): keep the text, and gate this
ticket on FEAT-03.

Option B (if we do not persist): update US-07 to:
- "`watch` is a poll loop that tracks pts/qts in memory only; missed
  events between invocations are expected."
- remove the `~/.cache/tg-cli/updates.state` reference.

## Acceptance
- US-07 acceptance matches the shipped code.
- `grep -rn updates.state docs/` returns zero hits beyond this
  decision.
