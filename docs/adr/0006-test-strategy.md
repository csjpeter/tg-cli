# ADR-0006: Three-level Test Strategy

**Status:** accepted (2026-04-19)
**Context:** supersedes the "unit tests + crypto known-answer" mix that
preceded the mock Telegram server (see ADR-0007).

## Context

Until FT-02 the project had two tiers:

- Unit tests under `tests/unit/` linked against `tests/mocks/crypto.c`
  and a stubbed socket. Fast, deterministic, but by construction they
  never prove that the real crypto path actually agrees with the mocks.
- A handful of "functional" tests that ran real OpenSSL against
  known-answer vectors — useful for catching divergence in the crypto
  primitives, but with no end-to-end RPC or domain coverage.

US-17 (functional coverage for every use case) forced the question:
where does a "functional" test stop and an "integration" test start,
and how do we keep them fast enough to run on every commit?

## Decision

We commit to **three levels**, every one of which runs in the standard
CI gate. No level is allowed to depend on the network, the filesystem
outside `/tmp`, or the developer's real Telegram account.

| Level | Location | Links against | What it proves |
|-------|----------|---------------|----------------|
| **Unit** | `tests/unit/` | `tests/mocks/crypto.c`, `tests/mocks/socket.c` (stub) | Module-local invariants, branch coverage, TL edge cases. |
| **Functional** | `tests/functional/` | Real `libssl`, `tests/mocks/socket.c` (in-process mock Telegram server) | End-to-end behaviour through real MTProto encryption: production TL parse, RPC, domain code drive a scripted server. |
| **Valgrind** | `./manage.sh valgrind` | Same as unit, Release build | Memory-safety regression gate (0 leaks, 0 errors). |

The mock crypto in the unit suite uses deterministic byte patterns with
call counters. The mock server in the functional suite uses the real
`crypto_*` wrappers (ADR-0004) against `libssl`. This gives us:

- **Unit**: fast, cheap to iterate, covers every branch including the
  adversarial ones that are awkward to stage through a real server.
- **Functional**: proves that the mock crypto and the real crypto agree,
  and that the domain layer works end-to-end against an MTProto-compliant
  server. If a unit test passes but the matching functional test fails,
  we have a spec drift bug between the mock and reality.

### Exclusions

- **No integration tier against real Telegram in CI.** Telegram's
  anti-abuse system will flood-wait test accounts and the test would be
  flaky. Developers can smoke-test against a scratch account locally,
  but the CI gate does not depend on the network.
- **No live CI Valgrind against the functional suite.** The mock server
  is Valgrind-clean by construction (allocated scratch buffers are
  freed in `mt_server_reset`), but the functional suite also sets up
  OpenSSL contexts per test and the wall-clock cost is not worth the
  marginal coverage on top of ASAN. ASAN + the unit Valgrind run are
  sufficient.

## Consequences

**Positive.**

- Every production code path has at least one test at the unit level
  (for branch coverage) and at least one test at the functional level
  (for round-trip wire correctness).
- Adding a new RPC is now cheap: one unit test for the TL encode/decode,
  one responder in the functional suite, done.
- Functional coverage is published separately (`docs/dev/coverage.md`)
  so regressions are visible at a glance.

**Negative.**

- Functional tests are about 3× slower per-case than unit tests because
  they exercise real AES-256-IGE. The suite is still ~10 seconds total
  with 427 cases, which is acceptable.
- Writing a functional test is ~2× more work than a unit test: the test
  author has to TL-encode the server's canned response. We accept that
  cost — see `docs/dev/mock-server.md` for the shared response builders.

## Related

- [ADR-0003 Custom Test Framework](0003-custom-test-framework.md) — the
  `ASSERT` / `RUN_TEST` macros in `tests/common/test_helpers.h`.
- [ADR-0004 Dependency Inversion](0004-dependency-inversion.md) — the
  `crypto_*` / `sys_*` wrapper layer that lets us swap real for mock at
  link time.
- [ADR-0007 Mock Telegram Server](0007-mock-telegram-server.md) — the
  functional-suite server implementation.
- [`docs/dev/testing.md`](../dev/testing.md), [`docs/dev/mock-server.md`](../dev/mock-server.md),
  [`docs/dev/coverage.md`](../dev/coverage.md).
