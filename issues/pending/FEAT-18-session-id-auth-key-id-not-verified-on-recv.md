# FEAT-18: Encrypted frames: auth_key_id and session_id not verified on receive

## Location
`src/infrastructure/mtproto_rpc.c` – `rpc_recv_encrypted()`

## Problem
The MTProto spec requires the receiver to verify that:
1. `auth_key_id` in the outer header matches `SHA256(auth_key)[24:32]`.
2. `session_id` in the decrypted plaintext matches the local session's
   `session_id`.

Current code discards both values silently:
```c
tl_read_uint64(&r); /* auth_key_id — skip */
...
tl_read_uint64(&pr); /* session_id */
```

Without check (1), a MITM replay attack using a different auth key is undetected
until the AES-IGE decryption produces garbage — and even then the check is
implicit (the inner SHA256 msg_key verification in `mtproto_decrypt`).  Without
check (2), a server could serve frames from a different session and the client
would silently accept them.

## Fix direction
After decrypting, compute `SHA256(auth_key)[24:32]` and compare with the
received `auth_key_id`; return `-1` on mismatch with a `LOG_ERROR`.  Compare
the plaintext `session_id` field with `s->session_id`; return `-1` on mismatch.

## Test
Unit test in `test_rpc.c`: build an encrypted frame with a wrong `auth_key_id`
and assert `rpc_recv_encrypted` returns -1.  Second test: frame with wrong
`session_id` in plaintext → -1.
