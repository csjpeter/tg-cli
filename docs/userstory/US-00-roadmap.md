# US-00 — Product Roadmap & Current Focus

## Vision
Three binaries sharing one MTProto 2.0 codebase:
`tg-cli-ro` (batch, read-only) · `tg-tui` (interactive) · `tg-cli` (batch r/w).
See `docs/SPECIFICATION.md` and `docs/adr/0005-three-binary-architecture.md`.

## Working end-to-end
- Phases 1–4 MTProto stack · ADR-0005 three-binary split
- US-05 me · US-04 dialogs (multi-entry) · US-06 history (self + @peer,
  simple-flag text extraction) · US-10 search (global + peer)
- US-07 watch poll loop · US-09 user-info · P7-01 contacts
- US-11 tg-tui MVP REPL (me / dialogs / history / contacts / info /
  search / poll / help / quit with '/' prefix and @peer resolution)
- Session persistence + `--logout` · `bad_server_salt` retry
- P4-04 DC migration · P4-07 service-message draining
- P5-07 tl_skip (phase 1: PeerNotifySettings; phase 2: Message trailer
  via fwd/reply/entities skippers and scalar optionals)
- Message text extraction for simple-flag messages
- 14 QA hardening fixes (crypto OOM, alignment, endianness, msg_id
  randomness, MITM hash verification, …)

## Known v1 limitations
- Messages with `media`, `reply_markup`, `reactions`,
  `restriction_reason`, `replies` set mark `complex=1` and halt
  iteration for that response. Tracked as **P5-09**.
- Dialog listings show peer id/unread only (titles require Chat/User
  skippers) — tracked as **P5-08**.
- 2FA accounts can't log in — needs P3-03 (SRP + PBKDF2-HMAC-SHA512).
- No media download yet — P6-01/03.
- No write capabilities (send/edit/delete/read-marker) — P5-03/04/06,
  P6-02, and the `tg-cli` batch binary are future work.

## Quality
- 1836 unit tests passing
- Valgrind: 0 leaks, 0 errors
- Zero warnings under `-Wall -Wextra -Werror -pedantic`
- Core+infra coverage (TUI excluded): **89.9%**

## Backlog by priority
1. **P5-07 phase 3** — Chat / User / MessageMedia skippers. Unblocks
   P5-08 title join, P5-09 complex messages, P6-01 file download.
2. **P3-03 2FA password (SRP)** — needs PBKDF2-HMAC-SHA512 wrapper.
3. **P6-01..03 media** — depends on phase 3.
4. **`src/domain/write/` + `tg-cli` binary** — P5-03 send-message and
   friends (US-12).

## Current focus
v1 consolidation is essentially complete. Next structural dependency
is **P5-07 phase 3 skippers** (Chat / User / MessageMedia), which
unlocks multiple downstream tickets at once.
