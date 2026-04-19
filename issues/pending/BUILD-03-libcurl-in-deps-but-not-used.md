# BUILD-03 — libcurl listed in manage.sh deps but not used by the project

## Category
Build / Dependency hygiene

## Severity
Low

## Finding
`manage.sh install_deps` installs `libcurl4-openssl-dev` (Ubuntu) and
`libcurl-devel` (Rocky), but `CMakeLists.txt` never calls
`find_package(CURL ...)` and no source file includes `<curl/curl.h>`.

This contradicts the dependency policy in `CLAUDE.md`: "Keep external
dependencies minimal."  Requiring `libcurl` at build time bloats Docker images,
slows CI `apt-get` steps, and confuses contributors who wonder where curl is used.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/manage.sh:38` — `apt-get install -y ... libcurl4-openssl-dev ...`
- `/home/csjpeter/ai-projects/tg-cli/manage.sh:49` — `dnf install -y ... libcurl-devel ...`
- `/home/csjpeter/ai-projects/tg-cli/CMakeLists.txt` — no CURL reference
- Grep across `src/` — no `<curl/curl.h>` include

## Fix
Remove `libcurl4-openssl-dev` / `libcurl-devel` from both install blocks in
`manage.sh`.  Update README deps table if applicable.
