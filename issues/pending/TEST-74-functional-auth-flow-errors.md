# TEST-74 — Functional coverage for auth flow error paths

## Gap
`src/app/auth_flow.c` is 17.9 % functionally covered. The happy
path of login is tested; none of the 10 documented Telegram error
strings from US-23 has a functional test.

## Scope
New suite `tests/functional/test_auth_flow_errors.c` with one test
per error string:

| test | server replies | asserts |
|---|---|---|
| `test_phone_number_invalid` | 400 PHONE_NUMBER_INVALID | exit 1, stderr matches "not a valid E.164" |
| `test_phone_number_banned` | 400 PHONE_NUMBER_BANNED | exit 1, stderr "banned" |
| `test_phone_code_invalid` | 400 PHONE_CODE_INVALID | retry prompt (interactive), exit 1 (batch) |
| `test_phone_code_expired` | 400 PHONE_CODE_EXPIRED | exit 1, stderr "expired" |
| `test_phone_code_empty` | 400 PHONE_CODE_EMPTY | exit 1, stderr "empty" |
| `test_session_password_needed` | 401 SESSION_PASSWORD_NEEDED | switch to 2FA prompt path |
| `test_password_hash_invalid` | 400 PASSWORD_HASH_INVALID | exit 1, stderr "wrong password" |
| `test_flood_wait` | 420 FLOOD_WAIT_30 | exit 1, stderr "retry after 0m30s" |
| `test_auth_restart` | 401 AUTH_RESTART | state reset, loop back to phone prompt |
| `test_sign_in_failed_generic` | 500 SIGN_IN_FAILED | exit 1, generic error |

## Acceptance
- 10 tests pass under ASAN.
- Functional coverage of `auth_flow.c` ≥ 70 %.
- Interactive tg-tui login exercises retry on `PHONE_CODE_INVALID`
  and `PASSWORD_HASH_INVALID` (up to 3 attempts).

## Dependencies
US-23 (the story). US-03 (login baseline).
