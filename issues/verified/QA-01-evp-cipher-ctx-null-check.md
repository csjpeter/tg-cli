# EVP_CIPHER_CTX_new() NULL check missing

## Description
`crypto_aes_encrypt_block()` and `crypto_aes_decrypt_block()` in `src/core/crypto.c`
(lines 46 and 59) call `EVP_CIPHER_CTX_new()` without checking for NULL return.
On OOM or resource exhaustion this causes a NULL pointer dereference and crash.

## Steps
1. Add NULL check after `EVP_CIPHER_CTX_new()` in both functions
2. Log to stderr and abort (OOM policy: minimal handling, immediate exit)

## OOM Policy
No graceful recovery needed. `fprintf(stderr, "OOM\n"); abort();` is sufficient.

## Estimate
~10 lines

## Dependencies
None

## Reviewed — 2026-04-16
Pass. Confirmed EVP_CIPHER_CTX_new NULL check with OOM abort at crypto.c lines 47-48 (encrypt) and 61-62 (decrypt). Diagnostic stderr message before abort.
