# Batch output format (plain/JSON)

## Description
With --json flag: JSON output. Otherwise: tab-delimited plain text. --quiet flag: data only, no status messages.

## Estimate
~200 lines (custom JSON writer ~100 lines + output formatter ~100 lines)

## Dependencies
- P8-01 (pending) — argument parser (--json, --quiet flag-ek kezelése)

## Verified — 2026-04-16
- `tg-cli-ro` subcommands (me, dialogs, history, search, watch,
  user-info, contacts) all emit either a tab-aligned plain-text
  table or a JSON array/object guided by `--json`.
- `--quiet` suppresses informational stderr noise in `watch`.
- Tests cover both paths through the domain modules.
