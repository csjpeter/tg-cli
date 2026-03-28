# MTProto 2.0 — Custom C Implementation Plan

## Summary

The Telegram **Bot API** (HTTPS JSON) only works as a bot and **cannot fetch chat history**.
For a real user client we must implement the **MTProto 2.0** protocol from scratch in C,
using POSIX sockets (transport) + libssl (crypto), **with no third-party libraries**.

---

## 1. Architecture — what to build

```
┌─────────────────────────────────────────────┐
│  Application (main.c + domain)              │
│  list chats, read messages, download media  │
├─────────────────────────────────────────────┤
│  Telegram API layer (TL RPC)                │
│  auth.sendCode, messages.getHistory, etc.   │
├─────────────────────────────────────────────┤
│  MTProto session                            │
│  msg_id, seq_no, salt, session_id, updates  │
├─────────────────────────────────────────────┤
│  MTProto encryption                         │
│  AES-256-IGE, SHA-256, auth_key management  │
├─────────────────────────────────────────────┤
│  TL serialization / deserialization         │
│  binary object encoding                     │
├─────────────────────────────────────────────┤
│  Transport (POSIX TCP socket)               │
│  Abridged/Intermediate encoding             │
└─────────────────────────────────────────────┘
```

---

## 2. Transport layer

### MTProto transport options:

| Transport | Description | Feasibility |
|-----------|-------------|-------------|
| **TCP (Abridged)** | Raw TCP, binary protocol | Best for real-time, requires own socket handling |
| **TCP (Intermediate)** | Raw TCP, 4-byte length prefix | Similar, slightly simpler |
| **HTTPS** | MTProto payload in HTTP POST | Possible via libcurl |
| **WebSocket (WSS)** | MTProto over WS | Requires libcurl WS support |

**Decision:** Use **TCP with Abridged transport** — this is what all real Telegram clients
use. POSIX sockets are part of the platform, no new dependency needed.

### Abridged transport encoding:
- First byte: `0xef` (marker)
- Then: 1-byte or 3-byte length prefix + encrypted payload
- Length 1 byte if payload < 127 bytes, otherwise `0x7f` + 2-byte little-endian length

### Data Centers (DCs):
| DC | IP | Region |
|----|-----|--------|
| 1 | 149.154.175.50 | Miami |
| 2 | 149.154.167.51 | Amsterdam |
| 3 | 149.154.175.100 | Miami |
| 4 | 149.154.167.91 | Amsterdam |
| 5 | 91.108.56.190 | Singapore |

Default port: 443 (same as HTTPS — passes through most firewalls).

---

## 3. Cryptography

### Required operations (all available via libssl/OpenSSL):

| Operation | OpenSSL function | Usage |
|-----------|-------------------|-------|
| SHA-256 | `SHA256()` | msg_key computation, KDF |
| AES-256-ECB | `AES_encrypt()` / `AES_decrypt()` | building block for IGE mode |
| RSA | `RSA_public_encrypt()` | Auth key generation |
| DH | manual `BN_mod_exp()` | Auth key generation |
| PRNG | `RAND_bytes()` | Random number generation |

### AES-256-IGE (Infinite Garble Extension):
**Not part of OpenSSL!** Must implement ourselves:

```
IGE_encrypt(plaintext, key, iv):
    for each 16-byte block i:
        c[i] = AES_encrypt(p[i] XOR iv_prev_c) XOR iv_prev_p
        iv_prev_c = c[i]
        iv_prev_p = p[i]
```

The IV is 32 bytes (two 16-byte blocks). First half is "prev ciphertext",
second half is "prev plaintext" initialization.

### AES key + IV derivation (MTProto 2.0):

```
x = 0 (client→server) or 8 (server→client)

msg_key_large = SHA256(substr(auth_key, 88+x, 32) + plaintext + padding)
msg_key = substr(msg_key_large, 8, 16)

sha256_a = SHA256(msg_key + substr(auth_key, x, 36))
sha256_b = SHA256(substr(auth_key, 40+x, 36) + msg_key)

aes_key = substr(sha256_a, 0, 8) + substr(sha256_b, 8, 16) + substr(sha256_a, 24, 8)
aes_iv  = substr(sha256_b, 0, 8) + substr(sha256_a, 8, 16) + substr(sha256_b, 24, 8)
```

---

## 4. Auth Key Generation (DH Key Exchange)

This is a 9-step process using **unencrypted** messages:

### Steps:

**1) req_pq_multi**
```
Send: nonce (random 128 bits)
Expect: ResPQ { nonce, server_nonce, pq, server_public_key_fingerprints }
```

**2) pq factorization**
- `pq` is a 64-bit number, product of two primes
- Pollard's rho algorithm (~20 lines of C)
- `p < q`, both prime

**3-4) req_DH_params**
```
Client generates: new_nonce (random 256 bits)
TL serialize: P_Q_inner_data_dc { pq, p, q, nonce, server_nonce, new_nonce, dc }
RSA_PAD encrypt → encrypted_data
Send: req_DH_params { nonce, server_nonce, p, q, fingerprint, encrypted_data }
Expect: Server_DH_Params { nonce, server_nonce, encrypted_answer }
```

RSA_PAD implementation (OAEP+ variant):
- data + padding = 192 bytes
- Byte reverse → data_pad_reversed
- temp_key = random 32 bytes
- data_with_hash = data_pad_reversed + SHA256(temp_key + data_with_padding)
- aes_encrypted = AES256_IGE(data_with_hash, temp_key, zero_iv)
- temp_key_xor = temp_key XOR SHA256(aes_encrypted)
- result = RSA(temp_key_xor + aes_encrypted, server_pubkey)

**5) Decrypt encrypted_answer**
```
tmp_aes_key = SHA1(new_nonce + server_nonce) + substr(SHA1(server_nonce + new_nonce), 0, 12)
tmp_aes_iv = substr(SHA1(server_nonce + new_nonce), 12, 8) + SHA1(new_nonce + new_nonce) + substr(new_nonce, 0, 4)
AES256_IGE_decrypt → Server_DH_inner_data { g, dh_prime, g_a, server_time }
```

**6-7) set_client_DH_params**
```
b = random 2048-bit number
g_b = pow(g, b) mod dh_prime
auth_key = pow(g_a, b) mod dh_prime   ← this becomes the 2048-bit auth_key!
TL serialize + encrypt → encrypted_data
Send: set_client_DH_params { nonce, server_nonce, encrypted_data }
```

**8-9) Response**
```
dh_gen_ok → auth_key successfully generated!
server_salt = substr(new_nonce, 0, 8) XOR substr(server_nonce, 0, 8)
```

### Built-in server public key:
The Telegram server RSA public key must be hardcoded in the client.
Source: [TDLib `PublicRsaKeySharedMain.cpp`](https://github.com/tdlib/td/blob/master/td/telegram/net/PublicRsaKeySharedMain.cpp)

---

## 5. TL (Type Language) serialization

TL is Telegram's binary serialization format. Every object has:
- 4-byte constructor ID (CRC32 hash of the definition)
- Fields in order, encoded by type

### Base type encoding:

| Type | Size | Description |
|------|------|-------------|
| int32 | 4 bytes | little-endian |
| int64 | 8 bytes | little-endian |
| int128 | 16 bytes | little-endian |
| int256 | 32 bytes | little-endian |
| float64 | 8 bytes | IEEE 754 |
| string/bare bytes | 4-byte length + data | padded to 4-byte boundary |
| bool | 0x997275b5 (true) / 0xbc799737 (false) | |
| vector | 0x1cb5c415 + int32 count + elements | |

### Implementation:
A full TL parser is not needed. Sufficient:
- `tl_write_int32(buf, val)`
- `tl_write_int64(buf, val)`
- `tl_write_string(buf, data, len)` — length prefix + padding
- `tl_read_int32(buf)`
- `tl_read_string(buf)` — length + data + skip padding
- Constructor IDs as a header file

---

## 6. MTProto message format

### Encrypted message:
```
[auth_key_id: 8 bytes] [msg_key: 16 bytes] [encrypted_data: N bytes]
```

### encrypted_data (after decryption):
```
[salt: 8 bytes] [session_id: 8 bytes] [message_id: 8 bytes]
[seq_no: 4 bytes] [message_data_length: 4 bytes]
[message_data: N bytes] [padding: 12..1024 bytes]
```

### Unencrypted message (during auth key generation):
```
[auth_key_id = 0: 8 bytes] [message_id: 8 bytes]
[message_data_length: 4 bytes] [message_data: N bytes]
```

### msg_id rules:
- Approximately `unixtime * 2^32`
- Even (client→server), odd (server→client)
- Monotonically increasing
- Within ±300 seconds of server time

### seq_no:
- `seq_no = current_seq * 2 + 1` (content-related message)
- `seq_no = current_seq * 2` (non-content-related message)

---

## 7. Authentication (user login)

After auth_key generation, over the encrypted channel:

### API methods:
1. `auth.sendCode` — send SMS/code to the given phone number
2. `auth.signIn` — sign in with phone number + code
3. `auth.checkPassword` — if 2FA is enabled

### API layer:
Every RPC call must be wrapped in:
```
invokeWithLayer(LAYER_VERSION, initConnection(api_id, api_hash, device, app_version, ...))
```

LAYER_VERSION: currently ~200+
api_id/api_hash: obtained from Telegram (free, requires registration at my.telegram.org)

---

## 8. Minimal API methods for the user client

### Auth:
- `auth.sendCode { phone, api_id, api_hash, settings }`
- `auth.signIn { phone, phone_code_hash, phone_code }`
- `auth.checkPassword { password }`

### Config:
- `help.getConfig` — DC information, configuration
- `help.getNearestDc` — nearest DC

### Chat list:
- `messages.getDialogs { offset_date, offset_id, offset_peer, limit }`
- `messages.getPeerDialogs { peers }`

### Messages:
- `messages.getHistory { peer, offset_id, offset_date, limit }` — **chat history!**
- `messages.getMessages { id }`
- `messages.search { peer, q, filter, limit }`

### Files:
- `upload.getFile { location, offset, limit }` — binary file download
- `photos.getUserPhotos { user_id, offset, limit }`

### Updates:
- `updates.getState` — query state
- `updates.getDifference { pts, date, qts }` — fetch new messages
- `updates.getChannelDifference` — channel updates

### Contacts:
- `contacts.getContacts`
- `contacts.search { q }`

---

## 9. Modules to build

### `src/core/tl_serial.h/c` — TL serialization
- write/read: int32, int64, int128, int256, string, bytes, vector, bool
- Builder + reader pattern (cursor-based)

### `src/core/ige_aes.h/c` — AES-256-IGE
- `aes_ige_encrypt(plaintext, len, key, iv, output)`
- `aes_ige_decrypt(ciphertext, len, key, iv, output)`
- Built on OpenSSL AES-ECB

### `src/core/mtproto_crypto.h/c` — MTProto encryption
- `mtproto_derive_keys(auth_key, msg_key, direction, aes_key, aes_iv)`
- `mtproto_encrypt(plain, auth_key, msg_key, output)`
- `mtproto_decrypt(cipher, auth_key, msg_key, output)`
- Padding generation (12..1024 bytes)

### `src/core/mtproto_auth.h/c` — Auth key generation
- `mtproto_gen_auth_key(transport, auth_key_out)`
- req_pq_multi → resPQ → factorization → req_DH_params → DH → set_client_DH → dh_gen_ok
- Server public key storage

### `src/core/mtproto_session.h/c` — Session management
- session_id generation
- msg_id generation (monotonic, time-based)
- seq_no tracking
- server_salt management (changes every 30 minutes)
- auth_key persistent storage (save to file, optional password protection)

### `src/infrastructure/tl_transport.h/c` — Transport layer
- TCP mode: POSIX socket + Abridged/Intermediate encoding
- HTTPS mode (fallback): libcurl POST with binary payload
- DC selection and migration

### `src/infrastructure/mtproto_rpc.h/c` — RPC framework
- RPC query sending (wrapper message)
- Response reception and dispatch
- Container support (multiple RPCs in one message)
- gzip_packed decompression
- msgs_ack acknowledgment
- Resend (bad_msg_notification)

### `src/infrastructure/telegram_api.h/c` — API methods
- TL constructor ID definitions (header)
- Auth: sendCode, signIn, checkPassword
- Messages: getDialogs, getHistory, getMessages
- Upload: getFile
- Updates: getState, getDifference

### `src/domain/telegram_service.h/c` — Domain logic
- telegram_connect() — load or generate auth_key
- telegram_login(phone) — SMS auth
- telegram_list_chats(limit, offset)
- telegram_get_history(chat_id, limit, offset_id)
- telegram_download_media(file_id, path)
- telegram_poll_updates() — real-time updates

---

## 10. Implementation order

### Phase 1: Foundations
1. TL serialization (tl_serial.h/c) + tests
2. AES-256-IGE (ige_aes.h/c) + tests
3. MTProto crypto (mtproto_crypto.h/c) + tests

### Phase 2: Auth key + Login
4. Transport layer TCP mode (tl_transport.h/c) + tests
5. Auth key generation (mtproto_auth.h/c) + tests
6. Session management (mtproto_session.h/c) + tests
7. RPC framework (mtproto_rpc.h/c) + tests

### Phase 3: Telegram API
8. API constructor IDs + serialization (telegram_api.h/c)
9. Auth flow: sendCode + signIn
10. Config + DC handling

### Phase 4: Features
11. Chat list (getDialogs)
12. Message history (getHistory)
13. File download (upload.getFile)
14. Real-time updates (updates.getDifference)

### Phase 5: TUI + Integration
15. Interactive TUI for chat list and messages
16. Batch mode CLI commands
17. Setup wizard (phone number + auth)

---

## 11. Performance and limits

| Parameter | Value |
|-----------|-------|
| auth_key size | 256 bytes (2048 bits) |
| Message overhead | ~40 bytes (header + minimum padding) |
| File download | upload.getFile, max 1 MB/chunk, multiple chunks |
| API Layer | ~200 (changes frequently, backwards compatible) |
| api_id/api_hash | Free, obtained at my.telegram.org |

---

## 12. Key takeaways

- **MTProto is not HTTPS JSON** — binary protocol with custom serialization (TL)
- **AES-IGE is not in OpenSSL** — must implement ourselves (~30 lines)
- **Auth key generation** is the most complex part (DH + RSA + multi-round communication)
- **Transport is raw TCP** — POSIX sockets, no libcurl needed for core MTProto
- **Chat history is available!** — `messages.getHistory` (Bot API cannot do this)
- **Phone number login** — SMS code + optional 2FA
- **Server public key** must be hardcoded in the client
- **api_id + api_hash** required from my.telegram.org

---

## References

- [MTProto protocol overview](https://core.telegram.org/mtproto)
- [MTProto detailed description](https://core.telegram.org/mtproto/description)
- [Creating an Authorization Key](https://core.telegram.org/mtproto/auth_key)
- [TL schema](https://core.telegram.org/schema)
- [API methods](https://core.telegram.org/methods)
- [TDLib server public key](https://github.com/tdlib/td/blob/master/td/telegram/net/PublicRsaKeySharedMain.cpp)
- [TDLib RSA operations](https://github.com/tdlib/td/blob/master/td/mtproto/RSA.cpp)
- [gotd/td — independent Go MTProto implementation](https://github.com/gotd/td)
- [Telethon — Python MTProto implementation](https://github.com/LonamiWebs/Telethon)
