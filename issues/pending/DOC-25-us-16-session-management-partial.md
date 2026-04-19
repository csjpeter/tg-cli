# DOC-25 — US-16 session management: several listed behaviours undocumented

## Gap
`docs/userstory/US-16-session-management.md` lists acceptance criteria
for session management that are not reflected in man pages or help
text:

1. "The binary can `--logout` and afterwards start fresh"
   → documented as flag, but not the behaviour (what happens on
   next start? Is a fresh DH handshake done? Or is there a prompt?).
2. "Session file is replaced atomically and never truncated mid-
   write" → FEAT-20 fixed this, but no user-visible documentation.
3. "If the session is revoked server-side, the next RPC fails with
   AUTH_KEY_UNREGISTERED (401); the client clears state and
   prompts a re-login" → this flow exists in code but is not in
   any man page or help text.
4. "A concurrent `tg-cli` save while `tg-tui` is loading yields a
   deterministic winner" → TEST-35 covers the test but no doc
   says so.

## Scope
1. Expand `.SH SECURITY` in all three man pages with a paragraph on
   session file semantics.
2. Add a `.SH FILES` entry documenting `session.bin.tmp` (atomic
   rename staging file) and `session.lock` (if introduced).
3. Add `.SH DIAGNOSTICS` entry for `AUTH_KEY_UNREGISTERED (401)`
   and what the client does about it.

## Acceptance
- Each acceptance criterion in US-16 is tied to a man page line.
- Cross-reference: TEST-58 (multi-binary) and TEST-35 (concurrent
  write) mentioned in the man page SECURITY section.
