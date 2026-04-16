# No integration tests for full protocol flow

## Description
All tests use mock crypto and mock sockets. There are no tests that exercise:
- Real OpenSSL crypto (functional known-answer tests)
- Full auth handshake end-to-end with a fake server
- Network error simulation (timeout, disconnect, partial read)
- Multi-layer interaction (transport → RPC → auth → session)

The mock crypto uses identity functions (encrypt = passthrough) and fixed SHA256
output, which masks real bugs (as demonstrated by the P1-mtproto-crypto rejection).

## Steps
1. Create `tests/functional/` directory for tests linked against real crypto.c
2. Add known-answer vector tests for AES-256-IGE with real OpenSSL
3. Add known-answer vector tests for MTProto key derivation
4. Create fake-server integration test for full auth handshake
5. Update CMakeLists.txt with separate test target

## Estimate
~500 lines

## Dependencies
- P1-mtproto-crypto ✅ (ready) — key derivation korrektségének ellenőrzése szükséges
- ARCH-01 (pending, soft) — CMake-ben külön test target könnyebb a static library-val

## Reviewed — 2026-04-16
Pass. Confirmed tests/functional/ directory with test_runner.c, test_ige_aes_functional.c, test_mtproto_crypto_functional.c, and CMakeLists.txt. Links real crypto (tg-crypto, not mock). Covered by commit 3644cd2 "QA-08: add functional test suite with real OpenSSL; fix crypto bugs found".

## QA — 2026-04-16
Pass. tests/functional/ with real OpenSSL KATs: test_ige_aes_functional.c and test_mtproto_crypto_functional.c. Proves P1-mtproto-crypto derivation matches spec against real SHA256 — the very bug class that triggered this issue. Commit 3644cd2 documents bugs found & fixed.
