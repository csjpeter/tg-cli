# FEAT-23 — Windows terminal_wcwidth() stub always returns 1 for all codepoints

## Category
Feature / Portability / TUI

## Severity
Medium

## Finding
`src/platform/windows/terminal.c:20`:
```c
int terminal_wcwidth(uint32_t cp) { (void)cp; return 1; }
```

This stub returns 1 for every codepoint including:
- Wide (CJK/emoji) characters that should return 2 — causing the TUI to
  render them as half-width and corrupt column alignment.
- Zero-width combining diacritics that should return 0 — causing the TUI
  to advance the cursor for invisible glyphs.
- Control characters — should return 0.

The POSIX implementation correctly delegates to `wcwidth(3)`.  The Windows
stub was noted as a known gap in `CLAUDE.md`'s portability table:
"needs bundled implementation".

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/platform/windows/terminal.c:20`
- `/home/csjpeter/ai-projects/tg-cli/CLAUDE.md` — portability table: `wcwidth(3)` on Windows `❌ needs bundled implementation`
- `/home/csjpeter/ai-projects/tg-cli/src/tui/screen.c:108` — uses `terminal_wcwidth(cp)` for column layout

## Fix
Bundle a known-good portable `wcwidth` implementation (e.g. Markus Kuhn's
public domain `mk_wcwidth`) in `src/vendor/` or `src/platform/windows/`.
Reference it from `src/platform/windows/terminal.c:terminal_wcwidth()`.
