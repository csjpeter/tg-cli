# TEST-39 — No automated test verifying tg-cli-ro does not contain write symbols

## Category
Test

## Severity
Medium

## Finding
ADR-0005 states: "If any source file under `src/domain/read/` contains a call
to a mutating API (e.g. `messages.sendMessage`), CI will fail because
`tg-cli-ro` won't compile/link.  A CI grep-check prevents `tg-cli-ro` from
accidentally pulling in write constructors."

However:
1. No such grep-check exists in CI or in the test suite.
2. The CMake link-time isolation (not linking `tg-domain-write`) only catches
   direct symbol references — it does not catch indirect pulls via shared code
   that has both read and write paths conditional on a flag.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/.github/workflows/ci.yml` — no `nm` / `objdump` / grep step
- `/home/csjpeter/ai-projects/tg-cli/docs/adr/0005-three-binary-architecture.md` —
  describes a "CI grep-check" that does not exist in practice

## Fix
Add a CI step (or `manage.sh` command) that:
```bash
nm --defined-only bin/tg-cli-ro | grep -E "messages_send|messages_edit|messages_delete|messages_forward|messages_upload" && echo "FAIL: write symbol in tg-cli-ro" && exit 1 || true
```
Or use `objdump -t` / `readelf -s` equivalently.  Run after Release build so
LTO does not strip symbols prematurely.
