# FEAT-25 — Bundled tinf vendor library version not pinned, patched, or documented

## Category
Feature / Dependency hygiene

## Severity
Low

## Finding
`src/vendor/tinf/` contains tinf version 1.2.1 (as declared in `tinf.h:33-35`).
The copyright is dated 2003-2019.  There is no documentation anywhere in the
project recording:

1. The exact version vendored (1.2.1).
2. Where it was obtained (GitHub: jibsen/tinf).
3. Whether any patches have been applied.
4. CVE status (tinf had a heap-overflow CVE — CVE-2018-14556 — fixed in 1.2.0;
   the bundled 1.2.1 post-dates this fix, but there is no record confirming
   the CVE was checked).

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/vendor/tinf/tinf.h:33` — `#define TINF_VER_MAJOR 1`, `MINOR 2`, `PATCH 1`
- `/home/csjpeter/ai-projects/tg-cli/src/vendor/tinf/tinflate.c:4` — copyright 2003-2019
- No `docs/` entry, no `src/vendor/README.md`, no comment in `CMakeLists.txt`

## Fix
1. Add `src/vendor/tinf/VENDORED.md` (or a comment block in `CMakeLists.txt`)
   recording: upstream URL, pinned version, commit hash, date vendored, patches
   applied (none), CVE check date.
2. Consider replacing with the current upstream 1.2.1+ or a maintained fork
   if newer releases are available.
3. Add a note in the main `README.md` third-party attribution section.
