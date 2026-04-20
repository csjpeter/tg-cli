# TEST-72 — Functional coverage for logger lifecycle and redaction

## Gap
`src/core/logger.c` is 2.4 % functionally covered. Rotation,
level filter, redaction, and directory creation are all uncovered
in end-to-end tests.

## Scope
New suite `tests/functional/test_logger_lifecycle.c`:

1. `test_logger_rotates_at_5mb` — force `logger_log` to write >5 MB,
   assert `session.log.1` exists and `session.log` is fresh (<5 MB).
2. `test_logger_level_filter_warn_drops_debug` —
   `setenv("TG_CLI_LOG_LEVEL","WARN")`, issue `LOG_DEBUG`, assert
   file does not contain the debug body.
3. `test_logger_redacts_message_body` — pipe a `messages.sendMessage`
   through the domain layer, assert the log file does NOT contain
   the plaintext body.
4. `test_logger_redacts_api_hash` — configure bootstrap with a known
   api_hash, assert the hash substring never appears in the log.
5. `test_logger_creates_missing_dir` — delete `~/.cache/tg-cli/logs`
   before run, assert first `logger_log` call recreates it
   mode 0700.
6. `test_logger_readonly_fallback_does_not_crash` — chmod log dir
   to 0500 mid-run, assert subsequent writes degrade to stderr
   without abort.

## Acceptance
- 6 tests pass under ASAN.
- Functional coverage of `logger.c` ≥ 75 %.
- FEAT-34 (rotation cap) and SEC-04 (redaction) can close on
  delivery of this ticket.

## Dependencies
US-21 (the story).
