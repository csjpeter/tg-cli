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
