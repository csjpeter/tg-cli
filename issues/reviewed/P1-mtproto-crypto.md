# MTProto Crypto (encrypt/decrypt)
src/core/mtproto_crypto.h/c

## QA Reject — 2026-03-31

### Critical: Key derivation deviates from MTProto 2.0 spec

`mtproto_derive_keys()` in `src/core/mtproto_crypto.c:17-47`:

1. **sha256_a wrong:** Code computes `SHA256(auth_key[x:x+96] + msg_key)`.
   Spec requires `SHA256(msg_key + auth_key[x:x+36])` — order reversed, slice too large.

2. **sha256_b wrong:** Code computes `SHA256(msg_key + auth_key[x:x+48])`.
   Spec requires `SHA256(auth_key[x+40:x+76] + msg_key)` — order reversed, offset/size wrong.

3. **aes_key/aes_iv assembly wrong:** Code uses 2-part split (8+24).
   Spec requires 3-part split: `sha256_a[0:8] + sha256_b[8:24] + sha256_a[24:32]` for key,
   `sha256_b[0:8] + sha256_a[8:24] + sha256_b[24:32]` for IV.

4. **msg_key auth_key slice wrong** (`mtproto_compute_msg_key` line 63):
   Code uses `auth_key[88+x .. 256]` (168 bytes). Spec requires exactly 32 bytes.

### Minor: Manual malloc/free instead of RAII
- `mtproto_compute_msg_key()` line 65: `malloc`/`free` should use RAII macro.
- `mtproto_encrypt()` line 108: same issue with `padded` buffer.

### Note
Unit tests pass only because mock SHA256 returns fixed output regardless of input,
masking all derivation bugs. Real Telegram server communication will fail.

## Reviewed — 2026-04-16
Pass. Confirmed mtproto_derive_keys matches MTProto 2.0 spec: sha256_a = SHA256(msg_key || auth_key[x:x+36]), sha256_b = SHA256(auth_key[x+40:x+76] || msg_key), 3-part splits for aes_key (8+16+8) and aes_iv. mtproto_compute_msg_key uses exact 32-byte slice at offset 88+x. Functional tests (tests/functional/test_mtproto_crypto_functional.c) verify with real OpenSSL.
