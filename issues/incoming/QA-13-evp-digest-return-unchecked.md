# EVP_Digest and EVP_EncryptUpdate return values not checked

## Description
Several OpenSSL EVP calls in `src/core/crypto.c` ignore return values:

- Line 21: `EVP_Digest(data, len, out, NULL, EVP_sha256(), NULL)` — returns 0 on failure
- Line 55: `EVP_EncryptUpdate(ctx, out, &out_len, in, 16)` — returns 0 on failure
- Line 56: `EVP_EncryptFinal_ex(ctx, out + out_len, &out_len)` — returns 0 on failure
- Line 69: `EVP_DecryptUpdate(ctx, out, &out_len, in, 16)` — returns 0 on failure
- Line 70: `EVP_DecryptFinal_ex(ctx, out + out_len, &out_len)` — returns 0 on failure
- Line 81: `EVP_Digest` for SHA-1 — same issue

If any of these fail (engine error, FIPS mode restriction, corrupted context),
the output buffer contains uninitialized/garbage data that flows into
encryption, key derivation, or message authentication — silently producing
broken ciphertext.

## Severity
CRITICAL — silent crypto failure leads to corrupted keys/ciphertext.

## Steps
1. Check return value of `EVP_Digest()` (must be 1)
2. Check return values of `EVP_EncryptUpdate`, `EVP_EncryptFinal_ex`
3. Check return values of `EVP_DecryptUpdate`, `EVP_DecryptFinal_ex`
4. On failure: `fprintf(stderr, "crypto: EVP operation failed\n"); abort();`
5. Consider changing `crypto_sha256`/`crypto_sha1` to return `int` for
   callers that can handle errors gracefully

## Estimate
~20 lines

## Dependencies
None (extends QA-01 which only covers EVP_CIPHER_CTX_new NULL check)
