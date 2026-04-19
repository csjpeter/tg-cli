# TEST-30: TL reader: no test for vector count claiming more entries than data

## Location
`tests/unit/test_tl_serial.c`, `src/core/tl_serial.c`

## Problem
There is no helper function `tl_read_vector_begin()` in `tl_serial.c` — vector
parsing is inline in each caller.  The concern is callers that read a `uint32_t`
count and iterate, trusting the count without checking remaining buffer capacity.

The `rpc_parse_container` function correctly bounds-checks via `tl_reader_ok`,
but callers in the domain layer (e.g. `domain_get_dialogs`, `domain_get_history`)
loop `for (i = 0; i < count; i++)` on server-supplied counts with only implicit
protection from the saturating `TlReader`.

## Missing test
A fuzz-style unit test that feeds a TL vector with `count = 0x7FFFFFFF` followed
by only 4 bytes of actual data; each element read saturates `r.pos = r.len` and
the loop should terminate naturally rather than spin or access out-of-bounds.
Verify that the consuming function returns an error (not UB / crash).

Add to `tests/unit/test_tl_serial.c`.
