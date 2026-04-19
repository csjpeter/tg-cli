# TEST-17 — functional test for null / missing / empty upload path

## Gap
US-14 acceptance:
> Null / missing path surfaces a clear error with non-zero exit.

Not exercised in the functional suite.

## Scope
Three cases:
1. `upload_null_path` — `domain_send_file(path=NULL)` returns
   non-zero without touching the wire.
2. `upload_nonexistent_path` — path points at a missing file; error
   surfaces with path in message.
3. `upload_empty_file` — 0-byte file: decide — either treated as a
   valid (empty) document, or rejected. Either way, make the behaviour
   deterministic and tested.

## Acceptance
- Three tests green; error-handling paths in
  `src/domain/write/upload.c` covered.
