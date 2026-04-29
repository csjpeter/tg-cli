# TEST-89 — Integration tests against Telegram test DC

## Gap

Every existing test runs against the in-process mock server.  No test
exercises the full MTProto stack end-to-end against real Telegram
infrastructure.  Subtle wire-format bugs, key-rotation edge cases, and
API behavioural changes go undetected until a user reports them in
production.

## Scope

New suite `tests/integration/` (infrastructure provided by FEAT-39).
Tests use real network I/O, the real Telegram test DC, and a pre-registered
test phone number in the `+999 …` range.  Telegram accepts magic code
`12345` for these numbers — no real SMS required.

### Test cases

1. `test_dh_handshake_completes`
   — Connect to test DC, complete the full DH key exchange, verify
   `auth_key` is non-zero and `session_id` is stable.

2. `test_login_send_code_and_sign_in`
   — Call `auth.sendCode` with the test phone, sign in with magic code
   `12345`, assert `auth.authorization` returned and session persisted
   to `session.bin`.

3. `test_get_self`
   — After login, call `users.getFullUser(inputUserSelf)`, assert the
   returned user id is non-zero and the phone field matches `TG_TEST_PHONE`.

4. `test_get_dialogs_returns_at_least_one`
   — Fresh test accounts have at least the "Telegram" service channel;
   assert `domain_get_dialogs()` returns ≥1 entry.

5. `test_get_history_smoke`
   — Pick the first dialog, call `domain_get_history()`, assert no error
   (empty history is acceptable for a fresh account).

6. `test_send_and_receive_message` *(requires two test accounts)*
   — Send a text message from account A to account B; call
   `updates.getDifference` on B and assert the message appears.
   Skip if `TG_TEST_PHONE_B` is unset.

7. `test_salt_rotation_survives_long_session`
   — Hold the session open for 90 s with periodic `ping` keepalives;
   assert no error is reported even if `bad_server_salt` is issued.

8. `test_logout_clears_session`
   — Call `auth.logOut`, assert `session.bin` is removed and a subsequent
   `domain_get_dialogs()` returns an auth error.

### Non-goals

- Speed: these tests may take tens of seconds; they are not part of the
  default `./manage.sh test` run.
- Isolation: test accounts share Telegram test infrastructure; tests must
  not assume a clean account state beyond what they set up themselves.

## Acceptance

- All 8 tests pass against Telegram test DC with ASAN enabled.
- Tests skip gracefully (exit 0 with message) when `TG_TEST_API_ID` is
  unset.
- No credentials, phone numbers, or auth keys committed to the repo.
- CI optional job green (see FEAT-39).

## Dependencies

FEAT-38 (configurable DC host + RSA key).
FEAT-39 (integration test infrastructure + `./manage.sh integration`).
