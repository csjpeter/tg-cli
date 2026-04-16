# US-00 — Product Roadmap & Current Focus

## Vision
Three binaries sharing one MTProto 2.0 codebase:
`tg-cli-ro` (batch, read-only) · `tg-tui` (interactive) · `tg-cli` (batch r/w).
See `docs/SPECIFICATION.md` and `docs/adr/0005-three-binary-architecture.md`.

## Done (verified)
- Phase 1: TL serialization, AES-256-IGE, MTProto crypto primitives
- Phase 2: TCP transport (abridged), MTProto session, RPC framing
- Phase 3-01: initConnection wrapper
- Phase 4: gzip inflate, msg container, rpc_error, constructor registry

## Ready for review
- P3-02 auth.sendCode + auth.signIn
- P8-01 argument parser · P9-01 readline
- ARCH-01…04 · QA-01…09

## Active implementation order (tg-cli-ro first)
1. ~~ADR-0005 directory split~~ · `src/app/`, `src/domain/read/`, binaries ✅
2. ~~US-05 self info~~ · `domain_get_self()` + `tg-cli-ro me` ✅
3. ~~P4-04 DC migration~~ · `auth_flow_login()` + PHONE/USER/NETWORK_MIGRATE ✅
4. ~~US-04 list dialogs~~ · minimal parser + `tg-cli-ro dialogs` ✅
5. ~~US-06 read history~~ · inputPeerSelf / Saved Messages ✅
6. ~~US-07 watch~~ · 30s poll loop + SIGINT ✅
7. ~~US-11 tg-tui MVP~~ · interactive readline shell ✅
8. **P3-03 2FA password** — SRP flow (blocker for 2FA users)
9. **US-09 resolve** — `contacts.resolveUsername` → unlocks non-self peers
10. **US-10 search** · **US-08 media download**
11. Full-text message parsing (v2 of US-06 / US-07)
12. Future: `src/domain/write/` + `tg-cli` (US-12)

## Quality gates
zero warnings · zero ASAN · zero Valgrind leaks · core+infra ≥ 90% (TUI deferred)

## Current focus
First vertical read-only slice (steps 1–7) is in place end-to-end. Next
dependencies: **P3-03 2FA** and **US-09 resolve-username** widen the peer
space beyond Saved Messages.
