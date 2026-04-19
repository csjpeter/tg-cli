# US-17 — Functional test coverage for every use case

Applies to: `tests/functional/`.

**Status:** done — FT-01 through FT-07 landed. 2703 unit + 427
functional tests green; combined line coverage is 88 % (functional
tests alone cover 52 % of the shared source).

## Story
As a contributor I want the functional test suite to exercise every
user-visible behaviour (US-03..US-16) end-to-end against a mock
Telegram server so that a regression in the RPC layer, auth flow, or
media pipeline surfaces in CI before it reaches a real account.

## Scope
The functional harness today covers crypto primitives only
(SHA-512, PBKDF2, IGE, MTProto crypto, SRP math/roundtrip, TL skip).
This story adds:

- A full scriptable Telegram server emulator on top of
  `tests/mocks/socket.c` — handshake, encrypted envelope
  parsing (auth_key_id routing, msg_key + IGE), RPC dispatch registry
  keyed by CRC32, canned response builders, salt/session/msg_id
  bookkeeping.
- Functional tests for login (SMS, 2FA, PHONE_MIGRATE, logout),
  read path (dialogs, history, search, user-info, contacts, watch),
  write path (send, edit, delete, forward, read markers), upload
  path (small/big file, photo, cross-DC), download path (photo,
  cross-DC FILE_MIGRATE).
- Separate lcov run producing `functional_coverage.info` + badge,
  hosted alongside the existing unit-test coverage on GitHub Pages.

## Acceptance
- Every US-03..US-16 acceptance bullet has at least one functional
  test asserting the observable signal.
- **Real OpenSSL on both sides.** The mock Telegram server runs
  in-process with the client and does its own AES-256-IGE +
  SHA-256 against the real `libssl` — the only thing mocked is the
  socket transport (in-memory buffer swap), so every functional
  test exercises the exact production crypto code paths. Handshake
  is usually bypassed via a pre-seeded `session.bin` with a known
  `auth_key`; one dedicated test still drives the full DH handshake
  via a fake server RSA keypair.
- Dedicated coverage badge in `README.md` next to the existing
  `coverage-badge.svg`.

## Dependencies
FT-01 (inventory) · FT-02 (mock server) · FT-03..FT-06 (tests) ·
FT-07 (coverage infra + badge).
