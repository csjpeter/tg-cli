# TEST-27 — functional test: TG_CLI_API_ID / TG_CLI_API_HASH env override

## Gap
`src/app/credentials.c:credentials_load()` checks `TG_CLI_API_ID` and
`TG_CLI_API_HASH` environment variables before reading the INI file
(positive env-var path). Only the **negative** case (both unset + no
config file → `credentials_load` returns -1) is tested in
`tests/functional/test_login_flow.c:558-574`.

There is no test that sets these env vars and verifies:
1. `credentials_load` returns 0 with the expected values.
2. Env vars take precedence over a conflicting INI file value.

US-03 batch mode depends on this: operators setting `TG_CLI_API_ID` in
their environment expect the env value to win.

## Scope
Add `test_credentials_env_override` (or similar) to
`tests/functional/test_login_flow.c`:
1. Write an INI file with `api_id=1111` / `api_hash=aaa`.
2. `setenv("TG_CLI_API_ID", "9999", 1)` and
   `setenv("TG_CLI_API_HASH", "zzz", 1)`.
3. Call `credentials_load`; assert it returns 0 and `cfg.api_id == 9999`.
4. `unsetenv` both variables and clean up.

## Acceptance
- Test passes under ASAN and Valgrind.
- `credentials.c` env-var code path is covered.
