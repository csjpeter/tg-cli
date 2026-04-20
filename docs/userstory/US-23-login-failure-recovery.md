# US-23 — Login failure recovery and retry flows

Applies to: all three binaries (interactive and batch login).

**Status:** gap — `src/app/auth_flow.c` is 17.9 % functional covered.
US-03 covers the happy path (phone → code → password). The long tail
of realistic failure modes is uncovered end-to-end.

## Story
As a user whose first login attempt goes wrong — wrong code, code
expired, SIM swap mid-flow, wrong 2FA password, Telegram's flood-wait
timer — I want the client to tell me exactly what happened and give
me a way to retry without dropping to a shell.

## Uncovered practical paths (all real Telegram error strings)
| server error | user-visible symptom |
|---|---|
| `PHONE_NUMBER_INVALID` | "Phone number is not a valid E.164 number." |
| `PHONE_NUMBER_BANNED` | "This number is banned from Telegram." |
| `PHONE_CODE_INVALID` | "Wrong SMS code — type the code again." |
| `PHONE_CODE_EXPIRED` | "Code expired — request a new one." |
| `PHONE_CODE_EMPTY` | "Code cannot be empty." |
| `SESSION_PASSWORD_NEEDED` | switch to 2FA prompt |
| `PASSWORD_HASH_INVALID` | "Wrong 2FA password." |
| `FLOOD_WAIT_x` | "Too many attempts; retry after <mm:ss>." |
| `AUTH_RESTART` | loop back to phone prompt, clean state |
| `SIGN_IN_FAILED` | generic fallback (log, exit 1) |

## Acceptance
- Mock server gains `mt_server_expect_error(crc, err_code, err_text)`
  (already exists) usage test.
- New functional test `tests/functional/test_auth_flow_errors.c` with
  one test per error string above, asserting:
  - stderr includes the documented human message.
  - exit code is 1 for fatal, 0 after successful retry.
  - session.bin is NOT created on any of the fatal paths.
- Interactive tg-cli / tg-tui login loops on recoverable errors
  (wrong code, wrong password) up to 3 attempts, then exits.
- Functional coverage of `auth_flow.c` ≥ 70 % (from 17.9 %).
- `docs/user/setup-my-telegram-org.md` + man page CONFIGURATION
  section add a "If login fails" section linking to these messages.

## Dependencies
US-03 (login flow). FEAT-37 wizard (already shipped). Pairs with
SEC-04 (api_hash redaction) since auth_flow logs credential-flavored
responses.
