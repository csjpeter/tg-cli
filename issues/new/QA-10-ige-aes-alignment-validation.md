# IGE encrypt/decrypt does not validate 16-byte alignment

## Description
`aes_ige_encrypt()` and `aes_ige_decrypt()` in `src/core/ige_aes.c` (lines 34, 66)
iterate in 16-byte blocks without verifying that `len` is a multiple of 16.
If `len % 16 != 0`, the final iteration reads past the input buffer boundary
(`plain[off+j]` or `cipher[off+j]` where `off + 15 >= len`), causing a
buffer over-read. Both functions return `void`, so callers have no way to
detect the error.

This is the **production** crypto path — not just test code.

## Severity
CRITICAL — buffer over-read in encryption hot path, potential information leak or crash.

## Steps
1. Add alignment check at the top of both functions: `if (len % 16 != 0) return;`
2. Consider changing return type to `int` so callers can detect the error
3. Add unit tests: call with `len = 15, 17, 31` and verify no crash / correct return

## Estimate
~10 lines code + ~20 lines tests

## Dependencies
None
