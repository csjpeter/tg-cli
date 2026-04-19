# DOC-19 — No SECURITY.md / responsible disclosure policy

## Category
Documentation / Security

## Severity
Low

## Finding
The repository has no `SECURITY.md` file and no disclosure instructions in
`README.md`.  GitHub's default security tab shows "no security policy" for the
repository.  Given that the tool handles Telegram auth keys (session.bin,
MTProto 2.0 crypto), credential files, and private message content, a
responsible disclosure path is important.

## Evidence
- `ls /home/csjpeter/ai-projects/tg-cli/SECURITY*` — no such file
- `/home/csjpeter/ai-projects/tg-cli/README.md` — no security / vulnerability reporting section

## Fix
Add `/home/csjpeter/ai-projects/tg-cli/SECURITY.md` with:
- Scope: what is in-scope for security reports (crypto, auth, session handling).
- Reporting path: e.g. email `peter@csaszar.email` or GitHub private
  vulnerability reporting.
- Expected response time and disclosure timeline.
