# config_store: missing explicit NULL check on config path

## Description
In `src/infrastructure/config_store.c:86`, `get_config_path()` can return NULL
if `platform_config_dir()` fails. The code passes this NULL directly to `fopen()`,
which will fail with an unclear error message.

## Steps
1. Add explicit NULL check after `get_config_path()`
2. Log a meaningful error message (e.g., "Failed to determine config directory")
3. Return -1 early

## Estimate
~5 lines

## Dependencies
None

## Reviewed — 2026-04-16
Pass. Confirmed config_store.c line 85-89: `get_config_path()` result checked, logger_log(LOG_ERROR, "Failed to determine config directory") + return -1 on NULL. fopen error also logged.

## QA — 2026-04-16
Pass. config_store.c line 85-89 checks get_config_path() for NULL, logs LOG_ERROR, returns -1 early. test_config.c covers the save path; no silent NULL-to-fopen anymore.
