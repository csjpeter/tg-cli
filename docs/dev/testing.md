# Developer Guide: Testing

## Running Tests

```bash
./manage.sh test        # Unit tests under AddressSanitizer
./manage.sh valgrind    # Unit tests under Valgrind
./manage.sh coverage    # Two-pass lcov (unit + functional) with HTML + badges
./manage.sh fuzz [N]    # libFuzzer harness for TL parser, N seconds (default 30)
```

There is no mechanism to run a single test in isolation. All unit tests
run together via `build/tests/unit/test-runner`; all functional tests run
via `build/tests/functional/functional-test-runner`.

Current status (as of 2026-04-19):

- **2703 unit tests** under ASAN — all green.
- **427 functional tests** against an in-process mock Telegram server —
  all green.
- 0 leaks, 0 errors under Valgrind.
- Combined line coverage: **~88 %** on the shared source (see
  `docs/dev/coverage.md`).

---

---

## Fuzz Testing (`tests/fuzz/`)

The libFuzzer harness in `tests/fuzz/fuzz_tl_parse.c` exercises every public
entry point in `tl_serial.h` and `tl_skip.h` (40+ functions) with
coverage-guided random inputs.

### Requirements

- **clang** must be installed (`apt-get install clang` on Ubuntu 24.04).
- The normal GCC build is **completely unaffected** — the fuzz target is
  built in a separate directory (`build-fuzz/`) only when requested.

### Usage

```bash
# 30-second run (default):
./manage.sh fuzz

# Custom duration (e.g. 5 minutes):
./manage.sh fuzz 300
```

CMake builds the `fuzz-tl-parse` executable with
`-fsanitize=fuzzer,address`.  The fuzzer starts from the seed corpus in
`tests/fuzz/corpus/` (8 seeds: empty, truncated, valid uint32, short/long
string, bool true/false, vector) and saves new interesting inputs to
`tests/fuzz/findings/`.

To enable in CMake manually:
```bash
cmake -DENABLE_FUZZ=ON -DCMAKE_C_COMPILER=clang build-fuzz/ ..
cmake --build build-fuzz/ --target fuzz-tl-parse
./build-fuzz/tests/fuzz/fuzz-tl-parse tests/fuzz/corpus/ -max_total_time=30
```

---

## Three-level Test Strategy (ADR-0006)

| Level | Links against | Purpose |
|-------|---------------|---------|
| **Unit** (`tests/unit/`) | `tests/mocks/crypto.c`, `tests/mocks/socket.c` (stub) | Module-local invariants with deterministic mocks. |
| **Functional** (`tests/functional/`) | Real OpenSSL, `tests/mocks/socket.c` (in-process mock Telegram server), production `tg-proto` / `tg-domain-*` / `tg-app` libraries | End-to-end behaviour: real MTProto encryption, real TL parsing, scripted server responses. |
| **Valgrind** | Same as unit, Release build | Memory-safety regression gate. |

See [ADR-0006](../adr/0006-test-strategy.md) for the rationale and
[ADR-0007](../adr/0007-mock-telegram-server.md) for the mock-server
architecture.

---

## Unit Tests (`tests/unit/`)

Every production module has at least one `test_*.c` file. Current
inventory (grouped by layer):

| Layer | Module | Test file |
|-------|--------|-----------|
| core | `fs_util.c` | `test_fs_util.c` |
| core | `logger.c` | `test_logger.c` |
| core | `arg_parse.c` | `test_arg_parse.c` |
| core | `readline.c` | `test_readline.c` |
| core | `tl_serial.c` | `test_tl_serial.c` |
| core | `tl_skip.c` | `test_tl_skip.c` |
| core | `tl_registry.c` | `test_registry.c` |
| core | `ige_aes.c` | `test_ige_aes.c` |
| core | `mtproto_crypto.c` | `test_mtproto_crypto.c` |
| core | `mtproto_session.c` | `test_phase2.c` |
| core | `crypto.c` (gzip TL variant) | `test_gzip.c` |
| infrastructure | `config_store.c` | `test_config.c` |
| infrastructure | `cache_store.c` | `test_cache.c` |
| infrastructure | `auth_session.c` | `test_auth_session.c` |
| infrastructure | `auth_2fa.c` | `test_auth_2fa.c` |
| infrastructure | `auth_transfer.c` | `test_auth_transfer.c` |
| infrastructure | `mtproto_auth.c` | `test_auth.c` |
| infrastructure | `mtproto_rpc.c` | `test_rpc.c` |
| infrastructure | `api_call.c` | `test_api_call.c` |
| infrastructure | `transport.c` | covered indirectly via `test_rpc.c` |
| app | `dc_config.c` | `test_dc_config.c` |
| app | `dc_session.c` | `test_dc_session.c` |
| app | `session_store.c` | `test_session_store.c` |
| domain/read | `self.c` | `test_domain_self.c` |
| domain/read | `dialogs.c` | `test_domain_dialogs.c` |
| domain/read | `history.c` | `test_domain_history.c` |
| domain/read | `search.c` | `test_domain_search.c` |
| domain/read | `contacts.c` | `test_domain_contacts.c` |
| domain/read | `user_info.c` | `test_domain_user_info.c` |
| domain/read | `updates.c` | `test_domain_updates.c` |
| domain/read | `media.c` | `test_domain_media.c` |
| domain/write | `send.c` | `test_domain_send.c` |
| domain/write | `edit.c`, `delete.c`, `forward.c` | `test_domain_edit_delete_forward.c` |
| domain/write | `read_history.c` | `test_domain_read_history.c` |
| domain/write | `upload.c` | `test_domain_upload.c` |
| tui | `screen.c`, `pane.c`, `list_view.c` | `test_tui_app.c`, `test_tui_dialog_pane.c`, `test_tui_history_pane.c` |
| platform | `posix/path.c`, `posix/terminal.c` | `test_platform.c` |

### Writing a New Unit Test

1. Add a `void test_foo(void)` function in the appropriate `test_*.c`.
2. Use `ASSERT(cond, "message")` from `tests/common/test_helpers.h`.
3. Register with `RUN_TEST(test_foo)` inside the module's `void run_*_tests(void)`
   suite entry point.
4. Add the suite to `tests/unit/test_runner.c` if it's a new file.
5. Zero-initialise structs with `= {0}` so Valgrind stays quiet.

```c
void test_foo(void) {
    Config cfg = {0};
    int result = foo(&cfg, 42);
    ASSERT(result == 0, "foo() should return 0 for a zero-initialised config");
}
```

---

## Functional Tests (`tests/functional/`)

Functional tests drive the production domain layer against an **in-process
mock Telegram server** implemented in `tests/mocks/mock_tel_server.c`. The
mock speaks real AES-256-IGE against `libssl` — only the socket is faked
(in-memory buffer swap via `tests/mocks/socket.c`). Most tests pre-seed
`session.bin` with a known `auth_key` so they skip the DH handshake; one
dedicated test still drives the full handshake via a fake server RSA
keypair.

Current suites (each file contains a `void run_*_tests(void)` entry):

| File | Coverage |
|------|----------|
| `test_ige_aes_functional.c` | AES-256-IGE known-answer tests against real `libssl`. |
| `test_mtproto_crypto_functional.c` | `msg_key` derivation, encrypt/decrypt round-trip. |
| `test_crypto_kdf_functional.c` | SHA-512 + PBKDF2-HMAC-SHA-512 (US-03 2FA path). |
| `test_srp_math_functional.c` | BN primitives and SRP math identities. |
| `test_srp_roundtrip_functional.c` | Full client ↔ server SRP round-trip. |
| `test_tl_skip_message_functional.c` | Kitchen-sink Message trailer iteration. |
| `test_mt_server_smoke.c` | FT-02 smoke: ping → pong through the mock. |
| `test_login_flow.c` | US-03: SMS, 2FA, PHONE_MIGRATE, `bad_server_salt`, `--logout`. |
| `test_read_path.c` | US-04..10: dialogs, history, search, user-info, contacts, resolve-username, watch. |
| `test_write_path.c` | US-12/13: send, reply, edit, delete, forward, read markers. |
| `test_upload_download.c` | US-14/15: small/big file upload, photo download, cross-DC FILE_MIGRATE. |

### Writing a New Functional Test

See [`docs/dev/mock-server.md`](mock-server.md) for the responder pattern,
`mt_server_expect(CRC, handler, ctx)`, `mt_server_seed_session(...)`, and
the `with_tmp_home()` helper.

---

## PTY-Backed Tests (`tests/functional/pty/`)

`libs/libptytest/` is a vendored copy of the PTY test library originally
developed in the sibling `email-cli` project. It opens a pseudo-terminal via
`forkpty(3)`, executes the program under test as a child process, feeds
keystrokes through the master file descriptor, and maintains a virtual
VT100 screen buffer. Test code inspects the buffer with `pty_screen_contains()`,
`pty_row_contains()`, and `pty_wait_for()`. The `tests/functional/pty/`
subdirectory holds PTY-backed smoke tests that verify the harness itself (no
tg-cli binary required) and will host future TUI acceptance tests (US-11,
US-02). The PTY target is only built on POSIX (`UNIX` CMake guard) and is
skipped gracefully on Windows cross-compile targets.

---

## Coverage Requirements

| Scope | Target | Current |
|-------|--------|---------|
| `src/core/` + `src/infrastructure/` combined | ≥ 90 % lines | ~92 % |
| `src/domain/` | best-effort | ~86 % |
| `src/app/` | best-effort | ~84 % |
| `src/tui/` | best-effort | ~78 % |
| `src/main/` | best-effort (wiring only) | ~40 % |

Full coverage workflow is documented in
[`docs/dev/coverage.md`](coverage.md).
