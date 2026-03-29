# MTProto Phase 2 — Transport + Auth Key + Session + RPC

## Context

Phase 1 (TL serialization, AES-256-IGE, MTProto crypto) is complete.
Phase 2 adds networking: TCP transport, session management, DH auth key
generation, and the RPC framework needed to send/receive encrypted messages.

---

## Module 4: Socket Wrapper (DIP)

### Files
- `src/platform/socket.h` — interface declarations
- `src/platform/posix/socket.c` — POSIX implementation
- `tests/mocks/socket.c` — fake server for integration tests
- `tests/mocks/mock_socket.h` — test accessor functions

### API
```c
typedef struct {
    int fd;
} SysSocket;

int      sys_socket_create(int domain, int type, int protocol);
int      sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t  sys_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t  sys_recv(int sockfd, void *buf, size_t len, int flags);
int      sys_close(int sockfd);
int      sys_set_nonblocking(int sockfd);
int      sys_getaddrinfo(const char *host, const char *port,
                         struct addrinfo **res);
void     sys_freeaddrinfo(struct addrinfo *res);
```

---

## Module 5: TCP Transport (Abridged)

### Files
- `src/infrastructure/transport.h/c`

### API
```c
typedef struct {
    int fd;
    int dc_id;
    uint8_t recv_buf[65536];
    size_t  recv_len;
} Transport;

int  transport_connect(Transport *t, const char *host, int port);
int  transport_send(Transport *t, const uint8_t *data, size_t len);
int  transport_recv(Transport *t, uint8_t *out, size_t max_len, size_t *out_len);
void transport_close(Transport *t);
```

### Abridged encoding
- Send: `0xEF` marker on connect, then for each packet:
  - If len < 127: 1-byte length prefix
  - Else: `0x7F` + 2-byte LE length
- Recv: read length prefix, then read payload

---

## Module 6: MTProto Session

### Files
- `src/core/mtproto_session.h/c`

### API
```c
typedef struct {
    uint64_t session_id;
    uint64_t server_salt;
    uint32_t seq_no;
    uint64_t last_msg_id;
    uint8_t  auth_key[256];
    int      has_auth_key;
} MtProtoSession;

void     session_init(MtProtoSession *s);
uint64_t session_next_msg_id(MtProtoSession *s);
uint32_t session_next_seq_no(MtProtoSession *s, int content_related);
void     session_set_auth_key(MtProtoSession *s, const uint8_t key[256]);
void     session_set_server_salt(MtProtoSession *s, uint64_t salt);
```

---

## Module 7: RPC Framework

### Files
- `src/infrastructure/mtproto_rpc.h/c`

### API
```c
typedef struct {
    uint64_t msg_id;
    int      is_response;
    TlWriter payload;
} RpcPending;

int  rpc_send_encrypted(MtProtoSession *s, Transport *t,
                        const uint8_t *tl_payload, size_t tl_len);
int  rpc_recv_message(MtProtoSession *s, Transport *t,
                      uint8_t *out, size_t *out_len);
```

Wire format (encrypted):
```
[auth_key_id: 8] [msg_key: 16] [encrypted(salt + session_id + msg_id + seq_no + len + data + padding)]
```

Wire format (unencrypted, during DH):
```
[auth_key_id = 0: 8] [msg_id: 8] [len: 4] [data]
```

---

## Module 8: Auth Key DH Exchange

### Files
- `src/core/mtproto_auth.h/c`

### API
```c
int mtproto_auth_key_gen(Transport *t, MtProtoSession *s);
```

### Steps
1. `req_pq_multi` → `ResPQ`
2. PQ factorization (Pollard's rho)
3. `req_DH_params` with RSA_PAD encrypted data
4. Decrypt `Server_DH_Params`
5. `set_client_DH_params`
6. Handle `dh_gen_ok` → compute auth_key

### Crypto needed beyond Phase 1
- SHA-1 (for DH temp key derivation) → add `crypto_sha1()` to crypto.h
- RSA public encrypt (OAEP variant) → add `crypto_rsa_public_encrypt()`
- Big number mod exp → add `crypto_bn_mod_exp()`

---

## Phase 2 Implementation Order

1. Socket wrapper + mock
2. TCP transport (abridged encoding)
3. MTProto session (msg_id, seq_no)
4. RPC framework (unencrypted + encrypted message framing)
5. DH auth key generation (depends on crypto additions)

---

## Files to create/modify

| Action | File |
|--------|------|
| Create | `src/platform/socket.h` |
| Create | `src/platform/posix/socket.c` |
| Create | `tests/mocks/socket.c` |
| Create | `tests/mocks/mock_socket.h` |
| Create | `src/infrastructure/transport.h` |
| Create | `src/infrastructure/transport.c` |
| Create | `src/core/mtproto_session.h` |
| Create | `src/core/mtproto_session.c` |
| Create | `src/infrastructure/mtproto_rpc.h` |
| Create | `src/infrastructure/mtproto_rpc.c` |
| Create | `src/core/mtproto_auth.h` |
| Create | `src/core/mtproto_auth.c` |
| Create | `tests/unit/test_transport.c` |
| Create | `tests/unit/test_session.c` |
| Create | `tests/unit/test_rpc.c` |
| Create | `tests/unit/test_auth.c` |
| Modify | `src/core/crypto.h` — add SHA-1, RSA, BN |
| Modify | `src/core/crypto.c` — implement new wrappers |
| Modify | `tests/mocks/crypto.c` — add mock implementations |
