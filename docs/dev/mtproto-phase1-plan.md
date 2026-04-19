> **Status: historical.** Retained for provenance. Phase 1 shipped in commits 4837b85..3fbdffd; see `docs/userstory/US-00-roadmap.md` for current status.

# MTProto Phase 1 — Implementation Plan

## Context

The project needs MTProto 2.0 protocol modules built from scratch. Requirements:
1. **Three-level testing**: unit → functional → integration (with fake server)
2. **Dependency Inversion (DIP)**: all system IO behind thin wrappers, CMake selects implementation
3. **No Dependency Injection** — no vtables, no runtime function pointers for system calls

---

## Dependency Inversion — System IO Wrappers

### Principle

System calls are wrapped in thin functions declared in headers. CMake selects the
implementation to link: `posix/`, `windows/`, or `tests/mocks/`. This is the **existing
pattern** in `src/platform/terminal.h` and `src/platform/path.h`, extended to all IO.

```
src/core/crypto.h           ← interface: crypto_sha256(), crypto_aes_encrypt(), ...
src/core/crypto.c           ← OpenSSL (platform-independent — OpenSSL everywhere)
tests/mocks/crypto.c        ← mock: deterministic values, call counters

src/platform/sysio.h        ← interface: sysio_fopen(), sysio_fclose(), ...
src/platform/posix/sysio.c  ← Linux/macOS: calls real fopen()
src/platform/windows/sysio.c← Windows: equivalent calls

src/platform/socket.h       ← interface: sys_socket(), sys_connect(), sys_send(), ...
src/platform/posix/socket.c ← POSIX: real socket calls
src/platform/windows/socket.c← Winsock
tests/mocks/socket.c        ← fake server for integration tests
```

### CMake test target links mocks instead of real implementations

```cmake
# tests/unit/CMakeLists.txt
if(BUILD_TESTING)
    list(REMOVE_ITEM TEST_SOURCES ${CMAKE_SOURCE_DIR}/src/core/crypto.c)
    list(APPEND TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/mocks/crypto.c)
endif()
```

### Mock infrastructure — `tests/mocks/crypto.c`

```c
static struct {
    int sha256_call_count;
    uint8_t sha256_output[32];
    int rand_bytes_call_count;
} g_mock_crypto;

void mock_crypto_set_sha256_output(const uint8_t hash[32]);
int  mock_crypto_sha256_call_count(void);
void mock_crypto_reset(void);

void crypto_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    g_mock_crypto.sha256_call_count++;
    memcpy(out, g_mock_crypto.sha256_output, 32);
}
```

### Wrappers needed for Phase 1

| Wrapper | Header | Why needed |
|---------|--------|-----------|
| `crypto_sha256()` | `src/core/crypto.h` | Key derivation, msg_key computation |
| `crypto_aes_encrypt_block()` | `src/core/crypto.h` | AES-IGE building block |
| `crypto_aes_decrypt_block()` | `src/core/crypto.h` | AES-IGE building block |
| `crypto_aes_set_key()` | `src/core/crypto.h` | AES key schedule setup |
| `crypto_rand_bytes()` | `src/core/crypto.h` | Padding generation |

Socket and sysio wrappers will be added in Phase 2 (transport + auth key).

---

## Module 1: TL Serialization — `src/core/tl_serial.h/c`

### Data structures

```c
typedef struct { uint8_t *data; size_t len; size_t cap; } TlWriter;
typedef struct { const uint8_t *data; size_t len; size_t pos; } TlReader;
```

### API

```c
// Writer
void   tl_writer_init(TlWriter *w);
void   tl_writer_free(TlWriter *w);
void   tl_write_int32(TlWriter *w, int32_t val);
void   tl_write_uint32(TlWriter *w, uint32_t val);
void   tl_write_int64(TlWriter *w, int64_t val);
void   tl_write_uint64(TlWriter *w, uint64_t val);
void   tl_write_int128(TlWriter *w, const uint8_t val[16]);
void   tl_write_int256(TlWriter *w, const uint8_t val[32]);
void   tl_write_double(TlWriter *w, double val);
void   tl_write_bool(TlWriter *w, int val);
void   tl_write_string(TlWriter *w, const char *s);
void   tl_write_bytes(TlWriter *w, const uint8_t *data, size_t len);
void   tl_write_vector_begin(TlWriter *w, uint32_t count);
void   tl_write_raw(TlWriter *w, const uint8_t *data, size_t len);

// Reader
TlReader tl_reader_init(const uint8_t *data, size_t len);
int      tl_reader_ok(const TlReader *r);
int32_t  tl_read_int32(TlReader *r);
uint32_t tl_read_uint32(TlReader *r);
int64_t  tl_read_int64(TlReader *r);
uint64_t tl_read_uint64(TlReader *r);
void     tl_read_int128(TlReader *r, uint8_t out[16]);
void     tl_read_int256(TlReader *r, uint8_t out[32]);
double   tl_read_double(TlReader *r);
int      tl_read_bool(TlReader *r);
char*    tl_read_string(TlReader *r);
uint8_t* tl_read_bytes(TlReader *r, size_t *out_len);
void     tl_read_raw(TlReader *r, uint8_t *out, size_t len);
void     tl_read_skip(TlReader *r, size_t len);
```

### Tests (~30): no mocks needed — pure data transformation

---

## Module 2: AES-256-IGE — `src/core/ige_aes.h/c`

### API

```c
void aes_ige_encrypt(const uint8_t *plain, size_t len,
                     const uint8_t *key, const uint8_t *iv, uint8_t *cipher);
void aes_ige_decrypt(const uint8_t *cipher, size_t len,
                     const uint8_t *key, const uint8_t *iv, uint8_t *plain);
```

### Tests (~10): mock crypto (block call counts) + functional (round-trip, known-answer)

---

## Module 3: MTProto Crypto — `src/core/mtproto_crypto.h/c`

### API

```c
void mtproto_derive_keys(const uint8_t *auth_key, const uint8_t *msg_key,
                         int direction, uint8_t *aes_key, uint8_t *aes_iv);
void mtproto_compute_msg_key(const uint8_t *auth_key,
                             const uint8_t *plain, size_t len,
                             int direction, uint8_t *msg_key);
size_t mtproto_gen_padding(size_t plain_len, uint8_t *out);
void mtproto_encrypt(const uint8_t *plain, size_t len, const uint8_t *auth_key,
                     int direction, uint8_t *out, size_t *out_len);
int mtproto_decrypt(const uint8_t *cipher, size_t len, const uint8_t *auth_key,
                    const uint8_t *msg_key, int direction,
                    uint8_t *plain, size_t *plain_len);
```

### Tests (~15): mock crypto (sha256 call counts) + functional (known-answer vectors, round-trip)

---

## Test Levels

| Level | How | Linked files | Scope |
|-------|-----|-------------|-------|
| **Unit** | `./manage.sh test` | `tests/mocks/crypto.c` | Mock crypto, pure logic |
| **Functional** | Separate test binary or conditional | Real `crypto.c` | Real crypto, known-answer vectors |
| **Integration** | Phase 2+ | `tests/mocks/socket.c` | Fake server, full protocol |

---

## Files to create/modify

| Action | File |
|--------|------|
| Create | `src/core/crypto.h` |
| Create | `src/core/crypto.c` |
| Create | `tests/mocks/crypto.c` |
| Create | `src/core/tl_serial.h` |
| Create | `src/core/tl_serial.c` |
| Create | `src/core/ige_aes.h` |
| Create | `src/core/ige_aes.c` |
| Create | `src/core/mtproto_crypto.h` |
| Create | `src/core/mtproto_crypto.c` |
| Create | `tests/unit/test_tl_serial.c` |
| Create | `tests/unit/test_ige_aes.c` |
| Create | `tests/unit/test_mtproto_crypto.c` |
| Modify | `tests/unit/test_runner.c` |
| Modify | `tests/unit/CMakeLists.txt` |
