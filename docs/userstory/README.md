# User Stories

Compact, behavior-focused stories that capture *why* a piece of work exists.
They complement the engineering-oriented tickets in `issues/` by expressing
the value and acceptance signals.

Format: one story per file, ≤30 lines.

| ID | Title | Applies to | Status |
|----|-------|------------|--------|
| US-00 | Product roadmap & current focus | all | living doc |
| US-01 | Increase core+infra test coverage >90% | infra | done (readline deferred) |
| US-02 | TUI modules become testable headlessly | tg-tui | backlog |
| US-03 | First-time user login works end-to-end | tg-cli-ro, tg-tui, tg-cli | partial |
| US-04 | List dialogs / rooms / channels | tg-cli-ro, tg-tui | backlog |
| US-05 | Show own profile (self info) | tg-cli-ro, tg-tui | backlog |
| US-06 | Read channel/chat history | tg-cli-ro, tg-tui | backlog |
| US-07 | Watch incoming messages (near-real-time) | tg-cli-ro, tg-tui | backlog |
| US-08 | Media download with local path display | tg-cli-ro, tg-tui | backlog |
| US-09 | Contacts and user-info lookups | tg-cli-ro, tg-tui | backlog |
| US-10 | Search messages | tg-cli-ro, tg-tui | backlog |
| US-11 | Interactive TUI (read-only MVP) | tg-tui | backlog |
| US-12 | Send message (batch + TUI) | tg-tui, tg-cli | future |

## Conventions

- **Persona:** "user" (tg-cli operator, typically a power user on Linux).
- **Story:** "As <persona>, I want <capability> so that <value>."
- **Acceptance:** bullet list of observable signals.
- **Binary scope:** a story names the target binary(ies) — `tg-cli-ro`
  never receives write features (ADR-0005).

See `docs/SPECIFICATION.md` for the full feature matrix.
