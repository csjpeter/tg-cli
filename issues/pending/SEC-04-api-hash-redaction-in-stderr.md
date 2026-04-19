# SEC-04 — api_hash redacted in logs only, not in --help/--version output

## Gap
SEC-02 (verified) added `redact_phone` and applied it in `logger_log`.
`api_hash` is currently only scrubbed in log files.  However:

- If `credentials_load` fails and prints the offending line to
  stderr (for debugging), the full api_hash may leak.
- Error messages like "config.ini parse error at line N" may echo
  the bad line verbatim.
- `--version` output is safe today, but a future addition of
  "`--version --verbose`" that dumps config would leak.

## Scope
1. Audit every `fprintf(stderr, ...)` in:
   - `src/app/credentials.c`
   - `src/app/bootstrap.c`
   - `src/infrastructure/config_store.c`
   for any place that may include a raw config line.
2. Wrap with `redact_secret(line, out)` that blanks out anything
   matching `[0-9a-f]{32}` (the api_hash pattern) regardless of key.
3. Add FT `tests/functional/test_api_hash_redaction.c`:
   - Write a config.ini with `api_hash=deadbeefcafebabef00dbaadfeedc0de`.
   - Corrupt one line (e.g. missing `=`).
   - Capture stderr from `credentials_load` and assert the hash
     appears as `[REDACTED]` (or is absent entirely).

## Acceptance
- No stderr path prints a full 32-hex token.
- SEC section of all man pages mentions the redaction contract.
