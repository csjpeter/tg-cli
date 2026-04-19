# TEST-38: DH auth: g value from server not validated (must be 2, 3, 4, 5, 6, or 7)

## Location
`src/infrastructure/mtproto_auth.c` – `auth_step_parse_dh()`

## Problem
MTProto spec requires that `g` (the DH generator) is one of {2, 3, 4, 5, 6, 7}.
The code reads `ctx->g = tl_read_int32(&inner)` but never validates it.  If a
malicious server sends `g = 1` (trivial group), `g = 0`, or `g = p-1`, the
computed `auth_key` degenerates and the session is insecure.

Additionally, `dh_prime_len` is checked (`prime_len > sizeof(ctx->dh_prime)`)
but the prime is not verified to be a safe prime (this is complex and optional),
and `g_a_len` has a similar unchecked bound.

## Fix direction
Minimal: after `ctx->g = tl_read_int32(&inner)`, add:
```c
if (ctx->g < 2 || ctx->g > 7) {
    logger_log(LOG_ERROR, "auth: invalid DH g=%d", ctx->g);
    return -1;
}
```

## Test
Unit test in `test_auth.c`: build a `server_DH_inner_data` with `g = 1` (and
with `g = 8`) and assert `auth_step_parse_dh` returns -1 for both.
