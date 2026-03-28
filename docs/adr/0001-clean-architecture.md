# ADR-0001: CLEAN Layered Architecture

**Status:** Accepted

## Context

A C CLI tool for Telegram can easily become a tangle of networking, config
parsing, and business logic in a single file. As the codebase grows, this makes
testing impossible and changes risky.

## Decision

The codebase follows a strict 4-layer architecture with zero upward dependencies:

```
Application  →  src/main.c
Domain       →  src/domain/
Infrastructure → src/infrastructure/
Core         →  src/core/
```

Each layer may only depend on layers below it. No circular dependencies.

### Layer Responsibilities

| Layer | Path | Responsibility |
|-------|------|----------------|
| Core | `src/core/` | Zero-dependency utilities: logger, fs_util, config, raii.h, json_util |
| Infrastructure | `src/infrastructure/` | External system adapters: config_store, cache_store, HTTP adapter (libcurl) |
| Domain | `src/domain/` | Business logic: telegram_service — coordinates API calls, does not know how config is stored |
| Application | `src/main.c` | CLI entry point, wires layers together |

### Dependency Inversion

Higher layers depend on data structures, not on lower-layer internals.
`telegram_service_*()` functions receive a `Config *` — they do not know or care
whether it came from a file, environment variable, or setup wizard.

### Doxygen

All public functions carry Doxygen-style comments (`@brief`, `@param`, `@return`)
so the codebase is self-documenting without a separate API reference.

## Consequences

- Unit-testing each layer in isolation is straightforward.
- Adding a new infrastructure adapter does not touch Domain or Core.
- `main.c` coverage is inherently low (wiring code); the >90% coverage goal applies
  only to Core and Infrastructure.
