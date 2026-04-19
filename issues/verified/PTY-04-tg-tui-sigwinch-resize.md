# PTY-04 — PTY test: tg-tui --tui SIGWINCH resize repaints screen

## Gap
TUI-09 landed SIGWINCH support (`tg_tui.c:746-758`), but no test verifies
that:
1. Sending SIGWINCH actually triggers a resize.
2. The new geometry is applied (narrow terminal clips left pane to ≥ 20
   cols and keeps history ≥ 20 cols per `pane.c:layout_compute`).
3. The screen is repainted without artefacts.

US-11 acceptance: "All terminal output UTF-8; supports CJK width via
wcwidth" — wide-glyph correctness after resize is also implicitly
required.

Depends on PTY-01 (libptytest) and PTY-02 (tg-tui startup test
infrastructure).

## Scope
Add `tests/functional/pty/test_tui_resize.c`:
1. Launch `tg-tui --tui` in an 80×24 PTY; wait for `[dialogs]` prompt.
2. Resize the PTY to 60×20 using `ioctl(TIOCSWINSZ)` on the master fd,
   then send SIGWINCH to the child pid.
3. Wait for `pty_settle(s, 200)` (screen settles).
4. Assert `pty_get_size` reports 60×20 and the screen still contains
   `[dialogs]`.
5. Resize back to 80×24; assert repaint succeeds.
6. Send `q`; assert clean exit.

## Acceptance
- Test passes under ASAN.
- No flicker artefacts (screen buffer consistent after resize).
- SIGTERM after resize still restores terminal (implicitly validated by
  clean exit check).
