# DH Auth Key Exchange
src/core/mtproto_auth.h/c — full 8-step DH exchange with tests

## QA Reject — 2026-03-31

### Architecture violation: core depends on infrastructure
- `src/core/mtproto_auth.h:19` includes `"transport.h"` (infrastructure layer)
- `src/core/mtproto_auth.c:12` includes `"mtproto_rpc.h"` (infrastructure layer)
- `AuthKeyCtx` holds `Transport *` and calls `rpc_send_unencrypted`/`rpc_recv_unencrypted`
- Fix: move module to `src/infrastructure/`, or invert the dependency

### No RAII for heap allocations
- `auth_step_parse_dh` uses raw `malloc`/`calloc` + manual `free` for `decrypted`,
  `padded`, `encrypted` across 6 exit paths — fragile, should use RAII macros

### Incomplete Doxygen on public functions
- `auth_step_req_pq`, `auth_step_req_dh`, `auth_step_parse_dh`,
  `auth_step_set_client_dh` (lines 78-87) lack `@param` and `@return` tags

## Reviewed — 2026-04-16
Pass. Confirmed module moved to src/infrastructure/mtproto_auth.{c,h} (layering correct — infra may depend on core+transport+rpc). RAII_STRING on pq_bytes, enc_answer, decrypted, prime_bytes, ga_bytes, padded, encrypted. auth_step_req_pq/req_dh/parse_dh/set_client_dh all have full Doxygen @param/@return.

## QA — 2026-04-16
Pass. Module relocated to src/infrastructure/mtproto_auth.c — layering correct (no core→infra violation). RAII_STRING on all heap buffers verified by Valgrind (0 leaks across test_auth.c and test_phase2.c). Public functions have full Doxygen @param/@return. All 8 DH handshake steps covered in tests/unit/test_auth.c.
