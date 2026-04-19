# FEAT-34 — Logger: size-based rotation for long-running tg-tui / watch sessions

## Gap
`~/.cache/tg-cli/logs/tg-cli.log` grows unbounded.  A user running
`watch` or TUI for days accumulates GBs of logs and eventually runs
out of disk.  No rotation, no max-size cap.

## Scope
1. Add `LOG_ROTATE_MAX_BYTES` (default 10 MiB) + `LOG_ROTATE_MAX_FILES`
   (default 5).
2. When `logger_log` detects size > cap, rename current → `.1`,
   shift `.N` → `.N+1`, drop oldest, open fresh current.
3. Respect `TG_CLI_LOG_MAX_BYTES` / `TG_CLI_LOG_MAX_FILES` env to
   override.
4. Pair with FT `tests/functional/test_logger_rotate.c`:
   - Write > 10 MiB of log lines.
   - Assert current file ≤ 10 MiB, `.1` exists, etc.

## Acceptance
- Disk usage bounded by `MAX_BYTES * MAX_FILES`.
- Existing `logger_log` interface unchanged.
- Rotation is atomic (rename-based, no partial files).

## Dependencies
- FEAT-21 (logger mode 0600) already applies to rotated files.
