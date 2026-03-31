# pq_factorize() truncates factors to uint32_t without validation

## Description
`pq_factorize()` in `src/infrastructure/mtproto_auth.c` (line 215) casts
the computed factors to `uint32_t`:
```c
*p_out = (uint32_t)p;
*q_out = (uint32_t)q;
```

The Telegram spec guarantees PQ is a product of two primes that fit in 32 bits,
but this is a server-provided value. A malicious or buggy server could send a
PQ whose factors exceed 2^32, causing silent truncation and incorrect
`p_q_inner_data` — the server would then reject the auth exchange with an
opaque error.

## Severity
MEDIUM — incorrect auth data from untrusted server input; no crash but
auth exchange fails with unhelpful error.

## Steps
1. Add validation after factorization: `if (p > UINT32_MAX || q > UINT32_MAX) return -1;`
2. Add unit test with a PQ whose factors exceed UINT32_MAX

## Estimate
~5 lines code + ~10 lines tests

## Dependencies
None
