# Developer Guide: Testing

## Running Tests

```bash
./manage.sh test        # Unit tests with AddressSanitizer
./manage.sh valgrind    # Unit tests with Valgrind
./manage.sh coverage    # Coverage report
```

There is no mechanism to run a single test in isolation — all unit tests run
together via `build/tests/unit/test-runner`.

## Unit Tests (`tests/unit/`)

Each source module has a corresponding test file:

| Test file | Module under test |
|-----------|-------------------|
| `test_fs_util.c` | `src/core/fs_util.c` |
| `test_logger.c` | `src/core/logger.c` |
| `test_config.c` | `src/infrastructure/config_store.c` |
| `test_cache.c` | `src/infrastructure/cache_store.c` |
| `test_platform.c` | `src/platform/posix/path.c` + `terminal.c` |

### Writing a New Test

1. Add a `void test_foo(void)` function in the appropriate `test_*.c` file.
2. Use `ASSERT(condition, "message")` for each assertion.
3. Register it with `RUN_TEST(test_foo)` in `test_runner.c`.

```c
void test_foo(void) {
    int result = foo(42);
    ASSERT(result == 0, "foo(42) should return 0");
}
```

### Struct Initialization

Always initialize stack-allocated structs with `= {0}` to avoid Valgrind
uninitialized-value warnings:

```c
Config cfg = {0};   // correct
Config cfg;         // wrong — cfg.ssl_no_verify is garbage
```

## Coverage Requirements

| Scope | Target |
|-------|--------|
| `src/core/` + `src/infrastructure/` combined | >90% line coverage |
| `src/domain/` | best-effort |
| `src/main.c` | best-effort (wiring code) |
