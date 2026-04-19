# ADR-0007: In-process Mock Telegram Server

**Status:** accepted (2026-04-19)
**Context:** US-17 FT-02 required a way to exercise the production
domain + RPC layers end-to-end without relying on real Telegram.

## Context

Unit tests (ADR-0006) prove that individual modules behave correctly
against deterministic mocks. They cannot prove that the full pipeline —
TL encode → AES-IGE encrypt → abridged transport → decrypt → TL parse —
agrees with a real Telegram server across an MTProto frame. Previously
we relied on manual smoke-tests against the live network, which:

- was flaky (rate-limits, 2FA prompts on every new session),
- leaked developer credentials into test logs,
- could not be run in CI at all.

We needed a scriptable MTProto server that speaks the real wire format
but lives inside the test process.

## Decision

Implement an **in-process mock Telegram server** under
`tests/mocks/mock_tel_server.{h,c}`. Both sides of the wire run real
OpenSSL; the only thing mocked is the TCP socket (`tests/mocks/socket.c`
bridges `send()` / `recv()` via an in-memory buffer pair).

### Architecture

```
+----------------------+        in-memory         +----------------------+
| production client    |  +------------------+   | mock_tel_server      |
| (tg-proto,            |  |  mock socket.c   |   | (real OpenSSL)       |
|  tg-domain-read,      |  |  send()/recv()   |   | msg_key + AES-IGE    |
|  tg-domain-write,     |<=>|  bridge          |<=>| responder dispatch   |
|  tg-app)              |  +------------------+   | keyed by CRC32       |
| real OpenSSL via      |                          |                      |
| crypto_* wrappers     |                          | scriptable via       |
+----------------------+                          | mt_server_expect()   |
                                                  +----------------------+
```

Key primitives (see `tests/mocks/mock_tel_server.h`):

- `mt_server_init()` / `mt_server_reset()` — lifecycle.
- `mt_server_seed_session(dc_id, ...)` — writes a valid
  `~/.config/tg-cli/session.bin` and primes the server with the matching
  `auth_key` + `server_salt` + `session_id`. Lets the client skip the
  DH handshake for >99 % of tests.
- `mt_server_expect(crc, fn, ctx)` — responder registry keyed by the
  innermost TL constructor CRC32. The server transparently unwraps
  `invokeWithLayer#da9b0d0d` + `initConnection#c1cd5ea9` so the test
  never has to.
- `mt_server_reply_result` / `mt_server_reply_error` — emit
  `rpc_result#f35c6d01` or `rpc_error#2144ca19`.
- `mt_server_push_update(tl, len)` — queue a server-initiated update for
  the next `recv()`.
- `mt_server_set_bad_salt_once(salt)` — one-shot `bad_server_salt` to
  verify the production salt-rotation loop in `api_call.c`.

A dedicated FT-02 smoke test (`test_mt_server_smoke.c`) drives one full
DH handshake to prove the server implementation itself is
spec-compliant; every other functional test starts from a seeded
session.

### Why in-process, not a separate binary?

- **Shared address space** lets the test set up fixtures (e.g. stub out
  a specific DC's `access_hash`) by writing to global state rather than
  serializing over a pipe.
- **No port allocation** / no test-fixture flakiness.
- **Coverage instrumentation works unchanged** — we get gcov data from
  the client-side production code directly, and the mock server itself
  sits outside the coverage scope.

### Why real OpenSSL on both sides?

If the server used mock crypto we would be validating our TL parsing
against our own encryption bugs. The mock socket is the minimum scope
of deception: everything above and below it is production code.

## Consequences

**Positive.**

- US-17 landed: every US-03..US-16 use case has a functional test.
- Adding a new RPC is a matter of writing one responder + one assertion.
- Tests run offline, deterministically, in < 10 s for the whole suite.
- Regressions in MTProto framing, salt rotation, msg_key derivation,
  and TL parse surface immediately in CI instead of in production.

**Negative.**

- The mock server duplicates the MTProto framing logic that the
  production `mtproto_rpc.c` also implements. Kept in sync by the
  smoke test (which round-trips the full handshake) and by
  cross-checking CRCs via `src/core/tl_registry.h`.
- Per-test state leaks are possible if a test forgets to call
  `mt_server_reset()` or `with_tmp_home()`. Enforced by convention +
  running the whole suite back-to-back in CI (leaks would cause
  follow-up tests to see stale state).

## Alternatives Considered

- **TDLib-backed integration tests.** Would pull in BSL-1.0 code that
  conflicts with this project's GPLv3 stance. Rejected.
- **Docker-based telegramd clone.** Operational overhead, slow CI,
  still needs scripted responses. Rejected.
- **Per-test forked Python server.** Language proliferation, serialises
  fixtures over a pipe, fragile teardown. Rejected.

## Related

- [ADR-0004 Dependency Inversion](0004-dependency-inversion.md) — the
  `crypto_*` / `sys_*` wrappers this design extends (socket swappable,
  crypto is **not** swapped in the functional suite).
- [ADR-0006 Test Strategy](0006-test-strategy.md) — the three-level
  testing commitment this server makes feasible.
- [`docs/dev/mock-server.md`](../dev/mock-server.md) — how-to guide for
  test authors.
