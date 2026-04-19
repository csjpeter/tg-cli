# TEST-36: tl_read_bytes: no test for 3-byte length field claiming huge allocation

## Location
`src/core/tl_serial.c` – `tl_read_bytes()`
`tests/unit/test_tl_serial.c`

## Problem
The 3-byte long-form TL bytes prefix can claim up to 16 MB
(`0xFE + 0xFF 0xFF 0xFF`).  `tl_read_bytes` checks `reader_has(r, total_raw)`
before allocating, so a claim that exceeds the buffer is caught — but the
`malloc(data_len)` call happens **before** the `reader_has` check in the current
code flow:

```c
size_t total_raw = header_size + data_len;
if (!reader_has(r, total_raw)) { r->pos = r->len; return NULL; }
unsigned char *result = (unsigned char *)malloc(data_len ? data_len : 1);
```

Wait — the `reader_has` check is at line 267, `malloc` at line 273.  So the
order is: check first, then alloc.  This is safe.  However there is no test
that:

1. Feeds a 4-byte buffer `[0xFE, 0xFF, 0xFF, 0xFF]` (claims 16 MB, buffer is
   only 4 bytes) and asserts `tl_read_bytes` returns NULL without allocating
   16 MB.
2. Feeds the exact maximum valid case: `[0xFE, 0x00, 0x01, 0x00]` followed by
   256 actual bytes — asserts a correct 256-byte result.

These test cases document the robustness of the out-of-bounds guard and would
catch any future reordering of the `malloc`/`reader_has` pair.

## Add to
`tests/unit/test_tl_serial.c`
