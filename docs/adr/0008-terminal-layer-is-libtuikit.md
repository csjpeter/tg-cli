# ADR-0008 — The terminal layer is libtuikit, linked statically

Status: Accepted (2026-10-11)

## Context

Until TG-11 the project carried its own terminal code: termios raw mode,
`ioctl(TIOCGWINSZ)`, `SIGWINCH`, `wcwidth(3)`, a pane layout and a line editor
(`src/platform/posix/terminal.c`, `src/tui/`). The owner now maintains a
terminal toolkit for all of the workspace's programs (`libtuikit`), and the
workspace TUI policy (`policies/tui-model.md`) requires programs to build on it.
The PTY tests use the owner's `libptytest` the same way.

## Decision

- The terminal, the TUI, the line editor and the masked password prompt are
  built on `libtuikit`; the PTY tests on `libptytest`.
- Both are consumed in one of two ways, selected by `-DWORKSPACE_LIBS=`
  `bundled|system|auto` (`policies/dependencies-model.md`):
  - **bundled**: git submodule under `deps/`, pinned to a release tag;
  - **system**: the installed `-dev` package. `auto` (the default) prefers it,
    tg-cli being proprietary.
- Only the `tuikit::tuikit_static` / `ptytest::ptytest_static` targets are
  linked, so no binary has a runtime dependency on `libtuikit.so` or
  `libptytest.so`. `ci/check-static-libs.sh` (`./manage.sh check-static-libs`,
  ctest `StaticWorkspaceLibs`) enforces it.
- Library sources are never copied into the tree, and a submodule is never
  edited in place: a change a library needs is a ticket in the library's
  project and a bump of the pin to the new release tag.
- `./manage.sh deps` installs `libtuikit-dev` and `libptytest-dev` (not in
  `bundled` mode, where it initialises the submodules instead).

## Consequences

- `src/platform/windows/*`, `src/platform/terminal.h` and `src/core/wcwidth.c`
  are preserved untested Windows preparation (owner's rule of 2026-10-10); on
  Linux the terminal is the library. See `docs/portability.md`.
- The static archives are built with the library's own flags. They link under
  the Release, ASAN debug, coverage and bundled builds (verified for TG-15).
  The libFuzzer harness `tg-proto` link set includes `libtuikit.a` and needs
  `clang`, which was not available when TG-15 was done: it is not verified.
- An open point of the owner (TG-15 comment, 2026-10-10): the policy's *system*
  way links the installed package dynamically; this project links the
  installed `libtuikit.a` statically, which the policy does not name.
