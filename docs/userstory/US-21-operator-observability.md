# US-21 — Operator observability: log file lifecycle and redaction

Applies to: all three binaries.

**Status:** gap — `src/core/logger.c` is 2.4 % functional covered.
Practical operator flows (long-running `tg-cli-ro watch`, cron jobs,
container deployments) depend on the logger, yet no functional test
drives it end-to-end against the file system.

## Story
As an operator running `tg-cli-ro watch` unattended, I want a log
file that:
- rotates before filling the disk,
- never leaks plaintext message bodies or credentials,
- keeps the last run's context around long enough to diagnose a
  failure I read about in the morning,
- is readable by my user and nobody else (mode 0600).

## Uncovered practical paths
- **Log rotation at 5 MB:** `logger_log()` crossing the size cap →
  `session.log` renamed to `session.log.1`, fresh file opened.
  Currently there is no functional test that grows the file past the
  cap — only an in-memory cap helper is unit-covered.
- **Log-level filter:** `TG_CLI_LOG_LEVEL=WARN` → `LOG_DEBUG` entries
  are dropped. No functional test verifies this.
- **Redaction of message bodies:** `logger_log()` must not contain the
  raw text of `messages.sendMessage`; only the RPC type + size. No
  functional test confirms.
- **Redaction of `api_hash`:** bootstrap log line must NOT contain the
  32-char secret (SEC-04 covers stderr, not the log file).
- **Missing directory:** `~/.cache/tg-cli/logs/` does not exist →
  logger must create it with mode 0700 or fail cleanly.
- **Write failure:** disk full or EROFS → logger must not crash; prod
  code should degrade to stderr.

## Acceptance
- New functional test suite `tests/functional/test_logger_lifecycle.c`:
  1. rotation at cap.
  2. level filter honoured.
  3. redaction of `messages.sendMessage` text.
  4. redaction of `api_hash`.
  5. log dir auto-creation.
- Functional coverage of `logger.c` ≥ 75 % (from 2.4 %).
- Man page §FILES entries for all three binaries gain a two-line
  paragraph describing rotation and redaction contract.
- FEAT-34 (rotation cap) and SEC-04 (redaction) reference this US.

## Dependencies
FEAT-33 (JSON log stream) stays optional / future work.
FEAT-34 rotation cap and SEC-04 redaction become sub-deliverables.
