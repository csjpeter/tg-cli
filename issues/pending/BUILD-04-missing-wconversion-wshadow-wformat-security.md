# BUILD-04 — Missing -Wconversion, -Wshadow, -Wformat-security compiler warnings

## Category
Build / Code quality

## Severity
Low

## Finding
`CMakeLists.txt` enables `-Wall -Wextra -Werror -pedantic` but omits several
commonly recommended additional warnings that catch real bugs:

| Flag | Catches |
|------|---------|
| `-Wconversion` | implicit narrowing: e.g. `int32_t date` printed via `%d` on LP64 |
| `-Wshadow` | local variable shadows outer declaration |
| `-Wformat-security` | `printf(user_str)` without format argument |
| `-Wstrict-prototypes` | functions declared without parameter types |
| `-Wnull-dereference` (GCC) | static null-deref paths |

`-Wformat-security` is particularly relevant given that the TUI history pane
renders user-controlled text and SEC-01 (ANSI injection) is already a known
concern.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt:9` — `add_compile_options(-Wall -Wextra -Werror -pedantic)`

## Fix
Add to `CMakeLists.txt`:
```cmake
add_compile_options(
    -Wall -Wextra -Werror -pedantic
    -Wconversion -Wshadow -Wformat-security
    -Wstrict-prototypes -Wnull-dereference
)
```
Fix any newly-surfaced warnings; suppress with explicit casts only where
the truncation is intentional and documented.
