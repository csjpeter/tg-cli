# TEST-42 — TL serial writer/reader roundtrip tests cover only fixed values

## Category
Test

## Severity
Low

## Finding
`tests/unit/test_tl_serial.c` contains `test_tl_roundtrip_*` tests but they
use only fixed constant inputs (e.g. `test_tl_roundtrip_int32` writes a single
value and reads it back).  There are no randomised / parameterised roundtrip
tests for:

- Arbitrary-length byte strings (boundary: 0, 253, 254 — the TL long-prefix
  threshold).
- Strings with embedded NUL bytes.
- Vectors of varying element count.
- Negative int32/int64 edge cases (INT32_MIN, -1).
- Double special values (NaN, ±Inf, ±0).

With the custom test framework offering no property-based harness, these need
to be written as explicit parameterised loops.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/tests/unit/test_tl_serial.c` — roundtrip tests use single fixed values

## Fix
Add a `test_tl_roundtrip_boundary_strings()` test that iterates lengths
0..260, writes a string of that length (filled with a known pattern), reads
it back, and asserts byte equality.  Similarly for int edge cases.  This
would have caught the long-prefix bug in TEST-36 category issues earlier.
