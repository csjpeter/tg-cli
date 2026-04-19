# TEST-58 — Functional test: tg-cli + tg-cli-ro + tg-tui share session safely

## Gap
All three binaries read/write `~/.config/tg-cli/session.bin` via
`session_store_{load,save}`.  FEAT-20 added `flock()` advisory
locking (LOCK_EX / LOCK_SH) and atomic rename.  TEST-35 covered
concurrent writes, but nothing covers the realistic scenario of:

1. `tg-tui` is running, holding a session open.
2. User in another terminal runs `tg-cli send …`.
3. `tg-cli` writes a new server_salt via `session_store_save`.
4. `tg-tui`'s in-memory session is now out of sync with disk.

Behaviour: `tg-cli` should be able to take the LOCK_EX, write
atomically, and succeed.  `tg-tui`'s in-memory state stays valid;
next disk load picks up the newer salt.

## Scope
Create `tests/functional/test_session_multi_process.c`:
- Spawn a child process that loads session, sleeps 200 ms with
  a held LOCK_SH.
- Parent concurrently calls `session_store_save` — must block up
  to 200 ms, then succeed.
- Assert total elapsed 200..400 ms, final session.bin content is
  the parent's write.
- No corruption (magic + version + payload survive).

## Acceptance
- Test passes under ASAN.
- Valgrind shows no file-descriptor leak on the child path.

## Dependencies
- FEAT-20 (flock) verified.
- Uses `fork` + pipe for coordination; keep Windows-conditional out.
