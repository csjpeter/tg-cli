# Developer Guide: Mock Telegram Server

This guide explains how to write a new functional test that drives the
production domain layer against the in-process mock Telegram server
implemented in `tests/mocks/mock_tel_server.c`. For the architectural
rationale see [ADR-0007](../adr/0007-mock-telegram-server.md).

## What the mock provides

Both sides of the wire run **real OpenSSL**. The only thing faked is the
TCP socket — `tests/mocks/socket.c` hands every `send()` directly to the
server and returns the server's queued bytes on the next `recv()`. That
means every functional test exercises the exact production AES-256-IGE +
SHA-256 code paths.

Wire format handled on both directions:

```
abridged_len_prefix + auth_key_id(8) + msg_key(16) + AES-IGE(
    salt(8) session_id(8) msg_id(8) seq_no(4) len(4) body(len) padding
)
```

The server also transparently unwraps `invokeWithLayer#da9b0d0d` +
`initConnection#c1cd5ea9` wrappers, so a responder only sees the inner
RPC CRC.

## Skeleton of a functional test

```c
#include "test_helpers.h"
#include "mock_tel_server.h"
#include "mock_socket.h"
#include "tl_serial.h"
#include "app/session_store.h"
#include "domain/read/dialogs.h"

#define CRC_messages_getDialogs 0xa0f4cb4fU
#define CRC_messages_dialogs    0x15ba6c40U

static void on_get_dialogs(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_messages_dialogs);
    /* ... TL-encode an empty dialogs envelope ... */
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void test_dialogs_empty(void) {
    with_tmp_home("dialogs-empty");
    mt_server_init();
    mt_server_reset();

    uint8_t auth_key[MT_SERVER_AUTH_KEY_SIZE];
    ASSERT(mt_server_seed_session(2, auth_key, NULL, NULL) == 0, "seed");
    mt_server_expect(CRC_messages_getDialogs, on_get_dialogs, NULL);

    DialogList list = {0};
    ASSERT(domain_get_dialogs(20, &list) == 0, "dialogs call succeeds");
    ASSERT(list.count == 0, "empty dialog list parsed");
    dialog_list_free(&list);

    ASSERT(mt_server_rpc_call_count() == 1, "one RPC dispatched");
    mt_server_reset();
}

void run_my_new_suite(void) {
    RUN_TEST(test_dialogs_empty);
}
```

Register the suite in `tests/functional/test_runner.c`:

```c
void run_my_new_suite(void);
/* ... */
run_my_new_suite();
```

## Key API

### `mt_server_init()` / `mt_server_reset()`

`init` is idempotent — safe to call before every test. `reset` wipes the
dispatch table, pending server frames, and the mock socket buffers.

### `mt_server_seed_session(dc_id, auth_key_out, salt_out, sid_out)`

Writes `~/.config/tg-cli/session.bin` with a fresh 256-byte `auth_key`,
`server_salt`, and `session_id`, and primes the server with the matching
credentials. The client reads this file on boot and **skips the DH
handshake**. Arrange `$HOME` first (see `with_tmp_home`).

### `mt_server_expect(crc, fn, ctx)`

Register a responder keyed by TL constructor CRC32. When the server
receives a frame whose innermost RPC starts with that CRC, `fn(ctx)`
fires. Repeat calls with the same CRC replace the handler.

Unregistered CRCs return a synthetic `rpc_error` with message
`NO_HANDLER_CRC_xxxxxxxx` so tests see failures early instead of hanging.

### `mt_server_reply_result(ctx, body, body_len)`

From a responder, emits `rpc_result#f35c6d01 req_msg_id:long result:Object`
wrapped around your body bytes.

### `mt_server_reply_error(ctx, code, msg)`

From a responder, emits `rpc_error#2144ca19 code:int msg:string`. Use this
to test server-side error paths (e.g. `FLOOD_WAIT_X`, `PHONE_CODE_INVALID`,
`FILE_MIGRATE_X`).

### `mt_server_push_update(tl, tl_len)`

Queues a server-initiated TL object (e.g. `updates_difference`) that lands
on the next `recv()` call. Useful for `watch`-loop / SIGWINCH tests.

### `mt_server_set_bad_salt_once(new_salt)`

Arms a **one-shot** `bad_server_salt#edab447b` rejection. The next frame
the client sends is bounced; the registered handler is not called. On the
retry, the flag has cleared and the RPC dispatches normally. Use this to
verify the production `api_call.c` salt-rotation loop.

Note: the bounced frame does **not** increment `mt_server_rpc_call_count()`
— that counter reports handled dispatches only.

### `mt_server_rpc_call_count()`

How many frames the dispatcher has handled since reset. Handy for asserting
"exactly N RPCs happened".

## Per-test `$HOME` isolation

Every test must redirect `$HOME` before `mt_server_seed_session` writes the
session file, otherwise tests leak state into the developer's real
`~/.config/tg-cli/`. The convention:

```c
static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}
```

`mt_server_seed_session` creates the directory structure as needed. Clean
up by letting the next test reset `$HOME` — `/tmp/tg-cli-ft-*` paths are
cheap to leave behind.

## Common TL response builders

The test suites share a few small helpers inline (not in a shared header —
each test is self-contained). Typical patterns:

- **User stub** — `TL_user#flags+id+access_hash+first_name+last_name+username...`
  Build with `tl_write_uint32(&w, TL_user)` then the flag mask and fields.
- **Peer** — `peerUser#9db1bc6d user_id:long`, `peerChat#35a95cb9 chat_id:long`,
  `peerChannel#a2a5371e channel_id:long`.
- **Updates wrapper** — `updates#74ae4240 updates:Vector<Update> users:Vector<User>
  chats:Vector<Chat> date:int seq:int`.
- **rpc_result** is emitted by the server helpers; don't build it by hand.

See existing suites for copy-paste-able builders:

- `tests/functional/test_login_flow.c` — auth flow + account.password (SRP)
- `tests/functional/test_read_path.c` — messages.dialogs, messages.messages,
  contacts.contacts, users.users, updates.state / difference
- `tests/functional/test_write_path.c` — updateShortSentMessage,
  updates (empty), messages.affectedMessages
- `tests/functional/test_upload_download.c` — upload.file, storage.filePartial,
  FILE_MIGRATE_X error path

## Debugging a failing functional test

1. Enable DEBUG logging in the test process:
   ```c
   logger_init("/tmp/ft.log", LOG_DEBUG);
   ```
2. Compare the mock's decoded envelope log against the client's outgoing
   frame. CRC mismatches are the most common cause ("handler fires on the
   wrong constructor").
3. Unknown-CRC responses show up as `NO_HANDLER_CRC_xxxxxxxx` in the
   client's `rpc_error` — look the CRC up in `src/core/tl_registry.h`.
4. If the handshake fires when it shouldn't, `session.bin` wasn't written
   where the client is reading it from — check `$HOME`.
