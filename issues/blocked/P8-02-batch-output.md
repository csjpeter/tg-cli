# Batch output format (plain/JSON)

## Description
With --json flag: JSON output. Otherwise: tab-delimited plain text. --quiet flag: data only, no status messages.

## Estimate
~200 lines (custom JSON writer ~100 lines + output formatter ~100 lines)

## Dependencies
- P8-01 (pending) — argument parser (--json, --quiet flag-ek kezelése)
