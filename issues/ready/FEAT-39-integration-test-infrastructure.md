# FEAT-39 — Integration test infrastructure against Telegram test DC

## Problem

All existing tests (unit, functional, PTY) run against an in-process mock
server.  They verify that the client code is internally consistent, but they
cannot catch regressions caused by subtle deviations from the real MTProto
wire format or Telegram API behaviour.  Telegram provides a dedicated test
environment (separate DCs, separate RSA key, free account creation) that is
safe to hammer without affecting real users.

## Story

As a developer, I want a `./manage.sh integration` command that runs a suite
of tests against the real Telegram test DC so that I can be confident the
client behaves correctly against actual Telegram infrastructure, not just
against our own mock.

## Scope

### manage.sh extension

```bash
./manage.sh integration        # runs integration suite (requires env vars)
./manage.sh integration login  # filter: login-related tests only
```

Skips gracefully with a clear message when credentials are absent.

### Environment variables (never stored in repo)

| Variable | Description |
|---|---|
| `TG_TEST_DC_HOST` | Test DC hostname (e.g. `149.154.175.10`) |
| `TG_TEST_DC_PORT` | Test DC port (default `443`) |
| `TG_TEST_RSA_PEM` | Test DC RSA public key (PEM, single line `\n`-escaped) |
| `TG_TEST_API_ID` | api_id from my.telegram.org test app |
| `TG_TEST_API_HASH` | api_hash from my.telegram.org test app |
| `TG_TEST_PHONE` | Pre-registered test account phone number |
| `TG_TEST_CODE` | SMS/app code (or `"auto"` to use Telegram's test magic code `12345`) |

Telegram test DC accepts a magic verification code `12345` for test phone
numbers in the `+999 …` range — no real SMS needed.

### CMake target

`tests/integration/` — new directory, separate CMake target
`tg-integration-test-runner`, linked against production code (not mock
socket or mock crypto).

### CI (GitHub Actions)

Optional job `integration` in `.github/workflows/`:
- `if: secrets.TG_TEST_API_ID != ''`
- Runs after `test` and `valgrind` jobs pass.
- Credentials injected as GitHub secrets.

## Acceptance

- `./manage.sh integration` exits 0 when `TG_TEST_API_ID` is unset
  (prints "integration tests skipped — set TG_TEST_* env vars").
- When credentials are present, the suite connects to the test DC,
  completes the DH handshake, and runs at least the smoke test.
- Full unit + functional suite still passes (no production code changes
  required beyond FEAT-38 for configurable DC/key).
- ASAN clean on the integration runner.

## Dependencies

FEAT-38 (configurable DC host + RSA key via config.ini — needed so the
integration runner can point at the test DC without recompiling).
TEST-89 (the actual integration test cases).
