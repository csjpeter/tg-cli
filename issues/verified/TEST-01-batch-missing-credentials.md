# TEST-01 — functional test for --batch + missing credentials

## Gap
US-03 batch-mode block:
> If a secret is not provided, the binary exits non-zero with a clear
> "needs credentials" message. Never reads `stdin` in `--batch`.

No functional test asserts this. An `app_bootstrap` regression (e.g.
prompting stdin in batch) would go undetected.

## Scope
1. `tests/functional/test_login_flow.c`: new test `test_batch_rejects_missing_credentials`.
2. Point `$HOME` at an empty tmp dir (no config.ini) and clear the
   env vars.
3. Invoke `cmd_me` (or similar read command) with `args.batch = 1`.
4. Assert exit code ≠ 0 and stderr mentions "credentials" (captured
   via `freopen`).

## Acceptance
- Test fires, passes, 0 ASAN / Valgrind errors.
