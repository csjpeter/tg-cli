# FEAT-20: session.bin has no file locking — concurrent processes corrupt state

## Location
`src/app/session_store.c` – `write_file()` / `upsert()`

## Problem
`write_file()` opens session.bin with `fopen(path, "wb")` (truncate + write)
without acquiring any file lock.  If two `tg-cli` processes run simultaneously
(e.g. a watch daemon and a one-shot `send`), both may call `upsert()` at the
same time:

1. Process A reads the file.
2. Process B reads the file.
3. Process A writes its updated entry.
4. Process B writes its own entry — overwriting A's changes.

The result is lost auth_key entries (silent silent logout on next start).  The
truncate + write pattern is particularly dangerous: if process B crashes mid-
write (e.g. disk full), the file is left truncated and both sessions are lost.

## Fix direction
Use `open(O_CREAT|O_RDWR)` + `flock(fd, LOCK_EX)` (POSIX advisory lock)
before read-modify-write.  Use an atomic rename pattern
(`write → session.bin.tmp`, then `rename → session.bin`) to guard against
truncation corruption.

Note: the platform abstraction layer (`platform/posix/`) is the right place for
`fs_advisory_lock()` / `fs_atomic_rename()` helpers; the Windows equivalent is
`LockFileEx` + `MoveFileExW`.

## Test
Functional test: two threads call `session_store_save()` concurrently ~1000
times on the same path; after all threads complete, load the store and assert
it is not corrupt (valid magic, sane count, all entries parseable).
