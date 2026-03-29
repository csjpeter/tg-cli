# ADR-0004: Dependency Inversion for System IO

**Status:** Accepted

## Context

Unit testing modules that call system or library functions (OpenSSL, POSIX sockets,
stdio) requires the ability to replace those calls with controlled substitutes.
In C, there are two common approaches:

- **Dependency Injection (DI):** Pass function pointers (vtables) as parameters at
  runtime. Every function gets an extra "adapter" parameter.
- **Dependency Inversion Principle (DIP):** Define thin wrapper functions in headers.
  Provide multiple implementations. The build system (CMake) selects which one to link.

## Decision

**Use DIP, not DI.** All system and library IO calls are wrapped in thin functions
declared in shared headers. CMake selects the implementation at link time:

- **Production:** `src/core/crypto.c` (OpenSSL), `src/platform/posix/socket.c` (POSIX)
- **Unit tests:** `tests/mocks/crypto.c` (deterministic mock), `tests/mocks/socket.c` (fake server)
- **Windows:** `src/platform/windows/socket.c` (Winsock)

No runtime indirection. No function pointer parameters. No vtable structs.

### Example

```c
// src/core/crypto.h — the interface
void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out);

// src/core/crypto.c — production (OpenSSL)
void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    SHA256(data, len, out);
}

// tests/mocks/crypto.c — test mock
static int g_sha256_count = 0;
void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    g_sha256_count++;
    memcpy(out, g_mock_hash, 32);  // deterministic
}
```

## Why not DI?

1. **Unnecessary complexity in C.** DI vtables add boilerplate to every function
   signature (`const CryptoAdapter *adapter` as first param). For a protocol stack
   where every function calls crypto, this pervades the entire codebase.
2. **No runtime polymorphism needed.** We never switch implementations at runtime.
   A test binary links mocks; a production binary links real code. Link-time
   substitution is sufficient.
3. **Consistent with existing patterns.** `src/platform/terminal.h` and
   `src/platform/path.h` already use this pattern successfully. CMake selects
   `posix/` or `windows/` implementations.

## Where to apply

Any function that touches an external resource or library:

| Category | Header | Implementations |
|----------|--------|----------------|
| Crypto (AES, SHA, RSA, RAND) | `src/core/crypto.h` | `crypto.c` / `tests/mocks/crypto.c` |
| TCP sockets | `src/platform/socket.h` | `posix/socket.c` / `windows/socket.c` / `tests/mocks/socket.c` |
| File I/O | `src/platform/sysio.h` | `posix/sysio.c` / `windows/sysio.c` / `tests/mocks/sysio.c` |
| Time | `src/platform/sysio.h` | included in sysio |

## Consequences

- Production code stays clean — no adapter parameters.
- Tests link mock implementations to control behavior and verify call patterns.
- Adding a new platform means adding a new `.c` file, not changing call sites.
- Mock implementations can expose call counters and setter functions for test assertions.
