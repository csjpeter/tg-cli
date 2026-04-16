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
1. **P4-04 DC migration** — PHONE_MIGRATE_X handling
2. **P3-03 2FA password** — SRP flow
3. **ADR-0005 directory split** — create `src/app/`, `src/domain/read/`,
   `src/main/tg_cli_ro.c`
4. **US-05 self info** — `users.getUsers(inputUserSelf)` (F-02)
5. **US-04 list dialogs** — `messages.getDialogs` (F-01)
6. **US-06 read history** — `messages.getHistory` (F-03)
7. **US-07 watch updates** — `updates.getDifference` loop (F-04)
8. **US-10 search** · **US-09 user-info** · **US-08 media download**
9. **ADR-0005 tg-tui entry** — REPL + TUI rendering (US-11)
10. Future: `src/domain/write/` + `tg-cli` (US-12)

## Quality gates
zero warnings · zero ASAN · zero Valgrind leaks · core+infra ≥ 90% (TUI deferred)

## Current focus
**Step 1: P4-04 DC migration** — required for most accounts.
