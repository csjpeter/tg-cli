# FEAT-33 — Structured JSON log format (opt-in) for observability pipelines

## Gap
`logger.c` writes lines of the form `[2026-04-19T12:34:56Z] [INFO]
foo=bar`.  Tools like Loki, DataDog, CloudWatch expect JSON:
`{"ts": "...", "level": "INFO", "msg": "foo=bar"}`.  Running tg-cli
as a service (see US-07 + FEAT-31 watch persistence) benefits from
structured logs for alerting on RPC errors.

## Scope
1. Add `TG_CLI_LOG_FORMAT=json|text` env (default "text" preserves
   current behaviour).
2. In `logger.c`, when format=json:
   - Emit `{"ts", "level", "pid", "comp", "msg"}` per line.
   - Escape `msg` via existing `json_escape_str`.
3. Document in `docs/dev/logging.md` + man page ENVIRONMENT section.

## Acceptance
- `TG_CLI_LOG_FORMAT=json tg-cli-ro watch --interval 2 2> >(jq)` pipes
  cleanly into jq.
- Text mode unchanged (byte-for-byte identical output vs. current).
- Paired FT: `test_logger_json_format.c` verifies key presence and
  types.

## Dependencies
- SEC-02 (redact_phone) still applies to `msg` regardless of format.
