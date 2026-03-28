# ADR-0002: RAII Memory Safety via GNU cleanup Attribute

**Status:** Accepted

## Context

Manual `free()` / `fclose()` / `curl_easy_cleanup()` at every return point leads
to leaks when error paths are added or refactored. `goto cleanup` patterns work
but clutter the happy-path logic.

## Decision

All resource-owning variables use GCC/Clang's `__attribute__((cleanup(fn)))` to
bind a cleanup function that fires automatically when the variable goes out of scope.
This is implemented in `src/core/raii.h` as macros:

| Macro | Type | Cleanup called |
|-------|------|----------------|
| `RAII_STRING` | `char *` | `free()` |
| `RAII_FILE` | `FILE *` | `fclose()` |
| `RAII_DIR` | `DIR *` | `closedir()` |

### Usage example

```c
int fetch(const Config *cfg) {
    RAII_STRING char *url  = NULL;

    if (asprintf(&url, "%s/bot%s/getUpdates", cfg->api_base, cfg->token) == -1)
        return -1;  // url freed automatically

    return curl_adapter_fetch(url, NULL, write_to_stdout);
}   // freed here, no matter which return path was taken
```

## Consequences

- No `goto cleanup` patterns anywhere in the codebase.
- Adding an early return never risks a resource leak — the compiler enforces it.
- Requires GCC or Clang (`-std=c11` + GNU extensions via `-D_GNU_SOURCE`).
  MSVC is not supported; this is a deliberate trade-off for a Linux-targeted tool.
- New resource types must get a cleanup function and macro added to `raii.h`.
