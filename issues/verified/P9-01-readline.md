# Custom readline (line editing, history)

## Description
Custom line editor built on the existing terminal.h abstraction. Features: cursor movement, deletion, history (up/down arrows), Home/End.

## Estimate
~500 lines

## Dependencies
- Platform terminal abstraction ✅ (implementálva) — terminal.h (raw mode, cursor)

Nincs pending függőség — önállóan indítható.

## Reviewed — 2026-04-16
Pass. Confirmed custom readline (309 LOC) + header (80 LOC) with cursor movement, Home/End, Backspace/Delete, Ctrl-A/E/D/W/K, history (Up/Down), non-TTY fgets fallback. Built on terminal.h abstraction. LineHistory circular buffer (256 entries). Full Doxygen.

## QA — 2026-04-16
Pass. readline module with cursor movement, history, Ctrl-* keybinds, non-TTY fgets fallback. Unit tests in test_readline.c hit all 13 testable functions. Raw terminal paths are not unit-tested (interactive), which is the normal coverage gap for TUI code; non-interactive batch path remains functional per CLAUDE.md.
