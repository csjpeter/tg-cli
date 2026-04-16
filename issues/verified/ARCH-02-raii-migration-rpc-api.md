# RAII migration: mtproto_rpc.c, api_call.c, mtproto_auth.c

## Description
Several infrastructure files use manual `malloc/free` with multiple return
paths instead of the project's RAII macros (`RAII_STRING`, `RAII_FILE` from
`src/core/raii.h`). This creates memory leak risk on early error returns.

Affected files and counts:
- `src/infrastructure/mtproto_rpc.c` — 9 manual free() calls, scattered across
  error returns (lines 51, 54, 63, 129, 132, 145, 149-150, 153, 162, 167)
- `src/infrastructure/api_call.c` — malloc + multiple error returns (lines 72-98)
- `src/infrastructure/mtproto_auth.c` — mixed: some RAII (lines 435, 496, 548),
  some manual free (lines 276-282, 426-442, 474-492)

The newer files (config_store.c, cache_store.c, mtproto_session.c) already
use RAII correctly — this is about bringing older code up to the same standard.

## Steps
1. In `mtproto_rpc.c`: replace `uint8_t *buf = malloc(...)` + scattered free()
   with `RAII_STRING uint8_t *buf = malloc(...)` — remove all manual free(buf)
2. In `api_call.c`: convert `wrapped` and `raw_resp` allocations to RAII_STRING
3. In `mtproto_auth.c`: convert remaining manual free() calls to RAII_STRING
   (pq_bytes, enc_answer, prime_bytes, ga_bytes)
4. Verify no double-free or use-after-free: `./manage.sh test && ./manage.sh valgrind`
5. Verify coverage maintained: `./manage.sh coverage`

## Estimate
~40 lines changed (remove free calls, add RAII_STRING annotations)

## Dependencies
Nincs — önállóan végrehajtható.

## Reviewed — 2026-04-16
Pass. Confirmed RAII_STRING on all heap allocations in mtproto_rpc.c (buf/encrypted/decrypted lines 49/97/126/144), api_call.c (wrapped/raw_resp lines 101/114), and mtproto_auth.c (pq_bytes/enc_answer/decrypted/prime_bytes/ga_bytes/padded/encrypted). No manual free() left on these paths. Valgrind clean.

## QA — 2026-04-16
Pass. 1735/1735 tests, 0 Valgrind leaks. RAII_STRING on all heap allocations in mtproto_rpc.c/api_call.c/mtproto_auth.c — no manual free() remain on error paths. Spot-checked that all error-return paths in test_rpc.c and test_auth.c exercise the RAII cleanup; Valgrind confirms no UAF or leak.
