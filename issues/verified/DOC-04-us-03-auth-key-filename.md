# DOC-04 — US-03 references the wrong session filename

## Gap
`docs/userstory/US-03-login-flow.md` lines 22 and 35 say the auth key
lives at `~/.config/tg-cli/auth.key`. The actual production code
(`src/app/session_store.c`) writes to `~/.config/tg-cli/session.bin`
(which also carries `dc_id`, `server_salt`, and session ids — not just
the auth key).

## Scope
Update US-03:
- Step 8: `~/.config/tg-cli/session.bin`
- Acceptance: `session.bin` file mode is 0600
- Note that the file also carries DC id + salt + session id, which is
  why it is called `session.bin`, not `auth.key`.

## Acceptance
- US-03 mentions only `session.bin`.
- Grep `auth.key` across `docs/` returns zero hits (or only in
  historical commit notes).
