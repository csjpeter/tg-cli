---
name: Testing Strategy
description: Three-level testing approach (unit/functional/integration) and DIP for mockability
type: feedback
---

## Three-level testing (mandatory)

Every module must have:

1. **Unit tests** — mock all system IO via DIP wrappers (`tests/mocks/`). Pure logic, deterministic, no network/crypto. Verify call counts, control return values.
2. **Functional tests** — link real implementations (real OpenSSL crypto). Known-answer test vectors from MTProto spec. Round-trip verification.
3. **Integration tests** — fake server (`tests/mocks/socket.c`). Full protocol flow: connect → auth key DH → encrypt → RPC → response.

**Why:** The user explicitly requires all three levels. Unit tests for speed and isolation, functional tests for real behavior verification, integration tests for protocol correctness against a controlled server.

**How to apply:** Every new module gets unit tests first (mock), then functional tests (real), then integration tests when the transport layer exists. The `./manage.sh test` target runs unit tests. Functional tests may be a separate binary or conditional.

## DIP over DI (mandatory)

**Never use Dependency Injection** (vtables, function pointer parameters, adapter structs passed at runtime).

**Always use Dependency Inversion** — thin wrapper functions in headers, multiple implementations, CMake selects at link time:
- `crypto_sha256()` in `crypto.h` → real OpenSSL in `crypto.c`, mock in `tests/mocks/crypto.c`
- `sys_socket()` in `socket.h` → real POSIX in `posix/socket.c`, fake in `tests/mocks/socket.c`

**Why:** DI adds unnecessary boilerplate in C (extra params everywhere). DIP is simpler, consistent with existing platform abstraction pattern, and sufficient since we never switch implementations at runtime.

**How to apply:** When a module needs to call any system/library function (OpenSSL, POSIX, stdio), first check if a wrapper exists in `crypto.h`, `socket.h`, or `sysio.h`. If not, add one. Never call `SHA256()`, `AES_encrypt()`, `socket()`, etc. directly from module code.

See ADR-0004: `docs/adr/0004-dependency-inversion.md`
