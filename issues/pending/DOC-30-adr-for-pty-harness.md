# DOC-30 — ADR: why we adopted libptytest (not pytest-expect / Rust ptyprocess)

## Gap
PTY-01 (verified) adopted `libs/libptytest/` copied from
`../email-cli`.  The decision rationale is not captured anywhere
other than the ticket itself.  Future contributors will wonder:

- Why not link against `util-linux` libutil?
  → `libutil` only gives `openpty()`.  We need screen parsing,
    assertion helpers, timeouts — all of which libptytest bundles.
- Why not depend on a Python / Rust expect-style harness?
  → Keeps test dep-tree pure-C.  No Python / cargo in CI.
- Why copy rather than git-submodule?
  → Per project policy (CLAUDE.md: minimise external deps).  A
    submodule would drag in email-cli's CMake + license.

## Scope
Write `docs/adr/0008-pty-test-harness.md`:
- Context: needed TTY-level tests.
- Options: libutil, Python pexpect, Rust ptyprocess, libptytest,
  home-grown.
- Decision: libptytest (copied).
- Consequences: we maintain a local fork; must watch upstream
  (`../email-cli`) for bug fixes and port them manually.

## Acceptance
- ADR file committed.
- ADR index (`docs/adr/README.md` if exists, else in `docs/README.md`)
  updated.
- Reference the ADR from `libs/libptytest/README.md` (or create one).

## Dependencies
- PTY-01 (verified).
