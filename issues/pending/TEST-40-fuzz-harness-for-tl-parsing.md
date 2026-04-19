# TEST-40 — No fuzz harness for TL binary parser

## Category
Test / Security

## Severity
Medium

## Finding
`src/core/tl_serial.c` and `src/core/tl_skip.c` parse untrusted binary data
received from the Telegram server (or any MITM between the client and a DC
proxy).  An existing ticket (TEST-30) covers a specific vector overflow, but
there is no coverage-guided fuzz harness that would systematically find
out-of-bounds reads, integer overflows, and infinite loops in the parser.

The bundled `src/vendor/tinf/tinflate.c` already has a fuzzer comment:
`/* clang -g -O1 -fsanitize=fuzzer,address -DTINF_FUZZING tinflate.c */`
showing the pattern is known but not applied to the project's own parser.

## Evidence
- `grep -r "fuzz\|AFL\|libfuzzer" src/ tests/` — zero hits in project code
- `/home/csjpeter/ai-projects/tg-cli/src/vendor/tinf/tinflate.c:618` — tinf fuzzer comment

## Fix
1. Add `tests/fuzz/fuzz_tl_serial.c` using libFuzzer's `LLVMFuzzerTestOneInput`:
   ```c
   int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
       TlReader r; tl_reader_init(&r, data, size);
       tl_skip_object(&r);  // or tl_read_uint32, etc.
       return 0;
   }
   ```
2. Add a CMake target built only when `-DENABLE_FUZZ=ON` is set.
3. Add a `./manage.sh fuzz` command.
4. Document in `docs/dev/testing.md`.
