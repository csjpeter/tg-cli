# TEST-08 — functional test: download cache reuse

## Gap
US-08 acceptance:
> Re-running the command reuses cached files (no re-download).

No test asserts this. A regression removing the cache hit would slip.

## Scope
1. First `download` call: responder fires once, file written.
2. Second call with the same target: assert
   `mt_server_rpc_call_count()` unchanged (no `upload.getFile`), file
   returned from cache.
3. A third call with `--no-cache` or an explicit re-download flag
   (if we add one under FEAT-*) re-fetches; otherwise this ticket is
   limited to parts 1-2.

## Acceptance
- Test green; cache hit path covered in `src/domain/read/media.c`.
