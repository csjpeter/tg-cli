# US-00 — Product Roadmap & Current Focus

## Vision
Three binaries sharing one MTProto 2.0 codebase:
`tg-cli-ro` (batch, read-only) · `tg-tui` (interactive) · `tg-cli` (batch r/w).
See `docs/SPECIFICATION.md` and `docs/adr/0005-three-binary-architecture.md`.

## Working end-to-end (commit-level)
- Phases 1–4 MTProto stack (verified)
- ADR-0005 directory split · two binaries `tg-cli-ro`, `tg-tui`
- US-05 `me` · US-04 `dialogs` (one per page) · US-06 `history` (self + @peer)
- US-07 `watch` poll loop · US-09 `user-info` · US-10 `search` (peer + global)
- US-11 tg-tui MVP REPL
- Session persistence + `--logout` · `bad_server_salt` auto-retry
- Message text extraction for simple-flag messages (no fwd/reply/media)
- P4-04 DC migration (PHONE/USER/NETWORK_MIGRATE handling)

## Known v1 limitations
- `dialogs` parses only the first Dialog entry (`PeerNotifySettings` skip
  is schema-fragile). Tracked for v2.
- `history` / `search` / `updates` stop after the first Message (same).
- Messages with `fwd_from`, `reply_to`, `media`, `reply_markup`,
  `via_bot_id` or `entities` flags are reported with `complex=1` and
  no text extraction. v2 needs a schema-table skipper.
- 2FA (P3-03) not yet implemented; accounts with 2FA will fail login.
- Media download (US-08) not implemented.
- Title join across `users`/`chats` vectors in dialog listings: deferred.

## Backlog by priority
1. **P3-03 2FA password (SRP)** — needed for 2FA accounts. Requires
   crypto wrapper additions (PBKDF2-HMAC-SHA512).
2. **Multi-entry parse v2** — iterate full vectors in history/search/
   dialogs/updates using a small schema-table or field skipper.
3. **Dialog title enrichment** — join dialog peer with users/chats vectors.
4. **US-08 media download** — `upload.getFile` chunked.
5. **Full-screen TUI** (US-11 v2) — panes, live redraw, kbd navigation.
6. Future: `src/domain/write/` + `tg-cli` batch r/w (US-12).

## Quality gates
zero warnings · zero ASAN · zero Valgrind leaks · core+infra ≥ 90% (TUI deferred)

## Current focus
v1 foundations are complete. Next impactful step: **P3-03 2FA** to unlock
2FA-enabled accounts, or **multi-entry parse v2** to surface more than
one message/dialog per call. Pick based on real-world testing feedback.
