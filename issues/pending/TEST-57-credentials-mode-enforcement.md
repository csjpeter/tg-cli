# TEST-57 — Functional test: credentials file permission enforcement

## Gap
`~/.config/tg-cli/config.ini` is created with mode 0600 (per CLAUDE.md).
Session file is also chmod 0600.  No FT verifies:

- If the config file already exists with mode 0644 (world-readable),
  `credentials_load` either refuses to start or chmod's it to 0600
  with a warning.
- If the parent directory is world-writable, the binary warns.
- Log files under `~/.cache/tg-cli/logs/` are created with mode 0600
  (FEAT-21 verified the open flags but no FT checks final mode).

A partial failure (file mode not enforced) leaks api_hash to other
users on multi-user systems.

## Scope
Create `tests/functional/test_credentials_perm_enforcement.c`:
- Pre-create `$HOME/.config/tg-cli/config.ini` with mode 0644
  containing a valid api_id/api_hash.
- Call `credentials_load(&cfg)`.
- Assert: either rc != 0 (refused) OR post-call file mode ==
  0600 (auto-repaired).  DECIDE + pin.
- Pre-create with mode 0777 on the dir → same as above.
- Check log files: after one `logger_log` call, the log file mode
  is 0600.

## Acceptance
- The contract for mode 0644 is documented and enforced.
- SEC section in `man/tg-cli*.1` reflects the enforced behaviour.

## Dependencies
- May spawn a FEAT ticket if the project decides to auto-repair
  rather than refuse.
