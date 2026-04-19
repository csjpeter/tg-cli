# DOC-18 — Output date fields are raw Unix epoch integers; format not documented

## Category
Documentation

## Severity
Low

## Finding
Plain-text and JSON outputs from `tg-cli-ro` emit message dates as raw 32-bit
Unix epoch integers (e.g. `"date":1713456000`), not as human-readable strings.
The history column header is simply `"date"` (printed via `%-10s`) with no
indication of timezone or format.

Neither the man page(s) nor `docs/` state:
- That `date` fields are Unix epoch seconds UTC.
- That `int32_t` is used for the date field, giving a year-2038 overflow when
  the Telegram API eventually uses timestamps beyond 2147483647 (Jan 2038).
- Whether the logger's `localtime()` timestamps and the output `date` integers
  are consistent.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/main/tg_cli_ro.c:351` — `printf("%-8s %-4s %-10s %s\n", "id", "out", "date", "text");`
- `/home/csjpeter/ai-projects/tg-cli/src/main/tg_cli_ro.c:341` — `printf("{...\"date\":%d...}", ..., entries[i].date, ...)`
- `/home/csjpeter/ai-projects/tg-cli/src/domain/read/history.h:31` — `int32_t date;`
- `/home/csjpeter/ai-projects/tg-cli/src/core/logger.c:96` — logger uses `localtime()`

## Fix
1. Document in man page(s) that date fields are Unix epoch seconds (UTC).
2. Consider rendering dates as ISO-8601 strings (`strftime + gmtime`) for
   plain-text output; keep integer in JSON for machine parsing.
3. Change `int32_t date` to `int64_t date` in domain structs to be
   year-2038 safe (the Telegram API sends 32-bit today but upgrading the
   storage type costs nothing).
