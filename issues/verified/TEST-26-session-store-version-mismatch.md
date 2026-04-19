# TEST-26 — unit test: session store rejects file with wrong version number

## Gap
`src/app/session_store.c:83` logs a warning and returns non-zero when
the session file's version field does not match `STORE_VERSION`:
```c
if (version != STORE_VERSION) {
    logger_log(LOG_WARN, "session_store: unsupported version %d", version);
    return -1;
}
```
`tests/unit/test_session_store.c` has `test_load_wrong_magic` (bad TGCS
header) but **no test for correct magic + wrong version**. If someone
changes `STORE_VERSION` without understanding the migration implications,
the guard is unverifiable by the test suite.

## Scope
Add `test_load_wrong_version` to `tests/unit/test_session_store.c`:
1. Write a file with the correct `TGCS` magic but `version = STORE_VERSION + 1`.
2. Call `session_store_load`; assert it returns non-zero.
3. Assert the file was not modified.

## Acceptance
- `test_load_wrong_version` passes under ASAN and Valgrind.
- `RUN_TEST(test_load_wrong_version)` added to the runner.
