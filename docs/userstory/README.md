# User Stories

Compact, behavior-focused stories that capture *why* a piece of work exists.
They complement the engineering-oriented tickets in `issues/` by expressing
the value and acceptance signals.

Format: one story per file, ≤30 lines.

| ID | Title | Applies to | Status |
|----|-------|------------|--------|
| US-00 | Product roadmap & current focus | all | living doc |
| US-01 | Increase core+infra test coverage >90% | infra | done (readline deferred) |
| US-02 | TUI modules become testable headlessly | tg-tui | done |
| US-03 | First-time user login works end-to-end (incl. 2FA + PHONE_MIGRATE) | tg-cli-ro, tg-tui, tg-cli | done |
| US-04 | List dialogs / rooms / channels | tg-cli-ro, tg-tui | done |
| US-05 | Show own profile (self info) | tg-cli-ro, tg-tui | done |
| US-06 | Read channel/chat history | tg-cli-ro, tg-tui | done |
| US-07 | Watch incoming messages (near-real-time) | tg-cli-ro, tg-tui | done |
| US-08 | Media download with local path display | tg-cli-ro, tg-tui | done |
| US-09 | Contacts and user-info lookups | tg-cli-ro, tg-tui | done |
| US-10 | Search messages | tg-cli-ro, tg-tui | done |
| US-11 | Interactive TUI (read + write, curses-style `--tui`) | tg-tui | done |
| US-12 | Send message + read markers | tg-tui, tg-cli | done |
| US-13 | Edit / delete / forward messages | tg-tui, tg-cli | done |
| US-14 | File + photo upload (small, big, auto-dispatch) | tg-tui, tg-cli | done |
| US-15 | Cross-DC media routing (upload + download) | tg-cli-ro, tg-tui, tg-cli | done |
| US-16 | Resolve `@username` + persistent session + `--logout` | all | done |
| US-17 | Functional test coverage for every use case | tests | in progress (FT-01..07) |

## Conventions

- **Persona:** "user" (tg-cli operator, typically a power user on Linux).
- **Story:** "As <persona>, I want <capability> so that <value>."
- **Acceptance:** bullet list of observable signals.
- **Binary scope:** a story names the target binary(ies) — `tg-cli-ro`
  never receives write features (ADR-0005).

See `docs/SPECIFICATION.md` for the full feature matrix.
