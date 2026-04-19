# DOC-28 — docs/dev/mock-server.md lacks recipes for new FT authors

## Gap
`docs/dev/mock-server.md` describes the architecture but does not
include copy-paste recipes for the common patterns that every new
functional test uses:

1. "Seed a session and connect" — 15-line boilerplate appears in
   every test file.  Extract into a helper + document.
2. "Arm one RPC and assert call count" — standard pattern.
3. "Use fake clock for TTL tests" — only TEST-04 uses it; the
   pattern is not documented elsewhere.
4. "Simulate a DC-migrate" — only test_upload_download.c shows the
   sequence; document it.
5. "Set up a stub TCP server for PTY tests" — pty_tel_stub.c is
   undocumented.
6. "Capture an RPC's arguments for later assertions" — no helper.

## Scope
1. Rewrite `docs/dev/mock-server.md` with a "Recipes" section
   covering the six patterns above.
2. Add helpers (if missing) to `tests/common/test_helpers.h`:
   - `setup_mock_session(MtProtoSession *s, Transport *t)` →
     replace the 15-line boilerplate.
   - `fake_clock_install()` / `fake_clock_advance_s(int n)` →
     unify the clock injection API across domain modules.
3. Convert one existing FT (pick `test_login_flow.c`) to use the
   helpers, as a worked reference.

## Acceptance
- Doc has working code snippets the author can paste.
- A newly written test (sample in appendix) compiles from the
  recipe without modification.
- Minimum 50% reduction in boilerplate in the converted FT.
