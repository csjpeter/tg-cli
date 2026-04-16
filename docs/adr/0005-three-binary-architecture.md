# ADR-0005 — Three-binary architecture

Status: Accepted (2026-04-16)

## Context

Until now the project produced a single binary `tg-cli`. The user clarified
the product shape: the suite needs three separate binaries so that operators
can opt into read-only vs. read-write behaviour by choice of binary, not by
a runtime flag.

## Decision

Ship three executables from one codebase:

| Binary | Mode | Capabilities | Write API calls |
|--------|------|--------------|------------------|
| `tg-cli-ro` | batch | read-only (perpetually) | **forbidden at compile time** |
| `tg-tui` | interactive TUI | read + (later) write | linked in as modules mature |
| `tg-cli`  | batch | read + write | all API calls |

Rationale:
- `tg-cli-ro` gives scripters a "safe" binary that cannot accidentally send or
  delete anything, even if a command-line parser bug were exploited.
- `tg-tui` is the primary interactive experience; write capability lands
  incrementally.
- `tg-cli` batch-mode write tooling comes last, once the interactive flows
  have flushed out the semantics.

## Compile-time enforcement of read-only

Write-capable domain modules live in `src/domain/write/`. The `tg-cli-ro`
target **must not** link that directory. CMake enforces this by splitting
domain into two static libs:

```
tg-domain-read    — dialogs, history, self, updates, search, user_info
tg-domain-write   — send, edit, delete, forward, read_markers, upload
```

`tg-cli-ro` links only `tg-domain-read`. `tg-tui` links both (gated by
feature flags as write features become available). `tg-cli` links both.

If any source file under `src/domain/read/` contains a call to a mutating
API (e.g. `messages.sendMessage`), CI will fail because `tg-cli-ro` won't
compile/link. A CI grep-check prevents `tg-cli-ro` from accidentally
pulling in write constructors.

## Shared machinery

All three share:
- `src/core/*` · `src/infrastructure/*` · `src/platform/*` (already
  organised this way)
- `src/app/auth.c` — phone/code/2FA/DC-migration login
- `src/app/config_app.c` — api_id / api_hash loading, first-run prompts

## Directory changes

```
src/
  app/              ← shared runtime wiring (auth, config, logging)
  domain/
    read/           ← no mutating API calls here (CI-enforced)
    write/          ← future: send/edit/delete/... (linked only by tg-tui, tg-cli)
  main/
    tg_cli_ro.c     ← batch, read-only entry
    tg_tui.c        ← TUI entry
    tg_cli.c        ← batch, read+write entry (later)
  tui/              ← rendering primitives used by tg_tui
```

`src/main.c` will be removed after the split is complete; its current
logger/config bootstrap moves to `src/app/bootstrap.c`.

## Consequences

- Three targets in CMake; shared code stays in one place.
- Unit tests stay per-module; per-binary smoke tests added later (`tests/e2e/`).
- Build time grows slightly; acceptable trade-off for the safety guarantee.
- Documentation (`README.md`, `docs/SPECIFICATION.md`) clearly names each
  binary's purpose so users pick the right one.
