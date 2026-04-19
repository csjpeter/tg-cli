# TEST-47 — Functional test: --config <path> loads alternate config

## Gap
Every binary's `print_usage()` advertises `--config <path>`.  There is
no FT that actually writes a config.ini at an arbitrary path and
confirms the binary reads from there (as opposed to the default
`~/.config/tg-cli/config.ini`).

A regression where `--config` is silently ignored would leave users
who rely on CI-style config files with no indication of failure.

## Scope
Create `tests/functional/test_config_path_flag.c`:
- Write a temp file `/tmp/tg-cli-ft-cfg-<pid>/custom.ini` with:
  ```
  api_id=99999
  api_hash=ba5eba11ba5eba11ba5eba11ba5eba11
  ```
- Drive `credentials_load` (or the higher wrapper) with
  `args.config_path = "/tmp/…/custom.ini"` and an `ApiConfig` out
  variable.
- Assert `cfg.api_id == 99999` and `strcmp(cfg.api_hash, "ba5e…")`
  equals zero.
- Repeat with a non-existent path → expect error with stderr hint.
- Repeat with a path whose mode is world-readable (0644) → either
  a warning or a refusal (DECIDE + pin the contract).

## Acceptance
- Happy-path load from custom file verified.
- Error path returns non-zero.
- SEC contract for 0644 file mode is documented and tested.
