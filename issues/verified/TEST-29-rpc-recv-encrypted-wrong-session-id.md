# TEST-29: No negative test for wrong session_id / auth_key_id in encrypted recv

## Relates to
FEAT-18 (fix not yet implemented); this ticket tracks adding tests once the
check is in place, but can also be used to document the *current* acceptance of
bad frames.

## Location
`tests/unit/test_rpc.c`

## Missing coverage
`rpc_recv_encrypted` currently silently ignores both the `auth_key_id` field and
the `session_id` field in the decrypted plaintext.  There are no tests that:

1. Feed a frame whose `auth_key_id` ≠ `SHA256(auth_key)[24:32]` and expect -1.
2. Feed a frame whose decrypted `session_id` ≠ `s->session_id` and expect -1.
3. Feed a truncated plaintext (dec_len < 32) and confirm the guard at line 164
   works correctly.

## Expected outcome (after FEAT-18 fix)
All three cases must return -1 with a `LOG_ERROR` entry.
