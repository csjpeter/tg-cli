# DOC-13 — TG_CLI_LOG_PLAINTEXT documented in all man pages but never read

## Gap
All three man pages (`man/tg-cli-ro.1`, `man/tg-cli.1`, `man/tg-tui.1`)
document the environment variable:
```
TG_CLI_LOG_PLAINTEXT
  If set to 1, include plaintext TL bodies at LOG_DEBUG.
```
A `grep` over the entire `src/` tree finds no `getenv("TG_CLI_LOG_PLAINTEXT")`
or equivalent call anywhere. The variable is dead documentation.

Two possible resolutions:
1. **Implement it**: add a `getenv` call during logger initialisation in
   `src/app/bootstrap.c` (or `src/core/logger.c`) that, when the variable
   equals `"1"`, sets a process-wide flag read by the RPC layer to include
   raw TL bytes in `LOG_DEBUG` messages.
2. **Remove it**: delete the ENVIRONMENT entries from all three man pages
   and the `--help` text with a note that the feature is future work.

Option 1 fulfils the stated security guidance ("enable only on a throwaway
account") and gives power users a diagnostic lever.

## Scope (Option 1)
1. Add `bool g_log_plaintext` global flag in `src/core/logger.h/c`.
2. Set it from `getenv("TG_CLI_LOG_PLAINTEXT")` in `app_bootstrap()`
   (`src/app/bootstrap.c`).
3. Gate any existing raw-TL dump calls (or add new ones) on that flag.
4. Unit test: `setenv("TG_CLI_LOG_PLAINTEXT","1",1)` → flag is true;
   unset → flag is false.

## Acceptance
- `TG_CLI_LOG_PLAINTEXT=1 tg-cli-ro me` emits TL bytes in the debug log.
- Default (env unset) produces no plaintext TL in the log.
- Man pages accurately describe the variable's effect.
