# DOC-27 — Man pages SEE ALSO sections are shallow / inconsistent

## Gap
The `.SH SEE ALSO` lines currently reference only the other tg-cli
binaries.  They should also reference:

- `jq(1)` — JSON consumption of --json output.
- `date(1)` — decoding Unix epoch seconds that tg-cli emits.
- `systemd.service(5)` — for users who run `watch` as a service.
- `flock(1)` — for users who wrap the CLI with flock to avoid
  session.bin contention.
- `docs/userstory/US-07-watch-updates.md` on the git tree (via a
  URL in comments).

## Scope
1. Update all three `.SH SEE ALSO` sections with the fuller list.
2. Where possible, add `.UR` links to the project's GitHub.
3. Add a `.SH NOTES` section pointing to `/etc/telegram-cli/...`
   if/when we ship a packaged service example.

## Acceptance
- Each man page SEE ALSO lists: all sibling binaries, plus jq(1),
  date(1), and flock(1).
- No broken cross-refs (run `mandb && man -w` to verify).
