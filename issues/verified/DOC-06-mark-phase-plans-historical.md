# DOC-06 — flag mtproto-phase1/phase2 plans as historical

## Gap
`docs/dev/mtproto-phase1-plan.md` and `docs/dev/mtproto-phase2-plan.md`
were written in March 2026 as implementation plans. Both phases are now
shipped (see `docs/userstory/US-00-roadmap.md`), but the files have no
status banner — readers can mistake them for live plans.

## Scope
1. Add a top-of-file banner to each:
   > **Status: historical.** Retained for provenance. Phase N shipped
   > in \<commit range\>; see `docs/userstory/US-00-roadmap.md` for the
   > current status.
2. Cross-link from `docs/README.md` with the "historical" qualifier
   (already done in the previous doc pass — verify it survives).

## Acceptance
- Both plan docs carry a "historical" banner in the first 10 lines.
- `docs/README.md` "Historical" entries remain in sync.
