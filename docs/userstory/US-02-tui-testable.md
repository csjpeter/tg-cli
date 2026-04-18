# US-02 — TUI modules become testable headlessly

**Status:** done — every curses-TUI module (`screen`, `pane`, `list_view`,
`dialog_pane`, `history_pane`, `status_row`, `app`) drives
`open_memstream` and the state-machine `TuiEvent` surface, which runs
headlessly in CI (TUI-01..09). Pure POSIX `readline.c` PTY coverage is
the still-deferred piece (see Acceptance).

## Story
As a developer, I want `readline.c` and `platform/posix/terminal.c` to be
unit-testable without a TTY so that line-editing and raw-mode logic can be
covered in CI.

## Why it's deferred
Both modules sit behind `isatty(3)`. The non-TTY branch is tested today; the
TTY-specific paths (raw mode, key reading, cursor math, `wcwidth`) are not.
Enabling them requires a PTY-based harness **or** extending the platform
abstraction with a swappable `terminal_io` interface.

## Acceptance (future)
- A test harness opens a pseudo-terminal (`openpty`) or injects a fake TTY.
- `readline.c` interactive branches (editing, kill-ring, history search) are
  covered ≥80%.
- `platform/posix/terminal.c` raw-mode + winsize paths ≥80%.

## Dependencies
- P9-02 full REPL depends on the same abstraction being stable.
