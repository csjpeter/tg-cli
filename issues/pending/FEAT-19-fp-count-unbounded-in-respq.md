# FEAT-19: Unbounded fingerprint vector iteration in ResPQ parsing

## Location
`src/infrastructure/mtproto_auth.c` – `auth_step_req_pq()`, lines ~294–310.

## Problem
```c
uint32_t fp_count = tl_read_uint32(&r);
for (uint32_t i = 0; i < fp_count; i++) {
    uint64_t fp = tl_read_uint64(&r);
    ...
}
```

`fp_count` is taken directly from the network without bounds checking.  If the
server (or a mock/fuzzer) returns `fp_count = 0xFFFFFFFF`, the loop runs
billions of iterations attempting to read 8 bytes each time.  `TlReader`
saturates at `r.pos = r.len` after the first overread, so it doesn't crash, but
it does spend O(4 billion) iterations calling `read_le64` and comparing the
return value, hanging the process for seconds.

This is a denial-of-service vector during the unauthenticated phase where the
server is not yet trusted.

## Fix direction
Add a compile-time reasonable bound, e.g. `#define MAX_FP_COUNT 16`, and check
before the loop:
```c
if (fp_count > MAX_FP_COUNT) {
    logger_log(LOG_ERROR, "auth: fp_count %u too large", fp_count);
    return -1;
}
```

## Test
Unit test in `test_auth.c`: build a ResPQ with `fp_count = 1000000` and assert
`auth_step_req_pq` returns -1 quickly (not hangs).
