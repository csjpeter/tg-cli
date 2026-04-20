# US-35 — PHONE_MIGRATE_x handling during first-time signup

Applies to: all three binaries' login flow.

**Status:** gap — `rpc_parse_error` parses `PHONE_MIGRATE_`,
`USER_MIGRATE_`, and `NETWORK_MIGRATE_` into `err.migrate_dc`. No
functional test drives a login where the server replies
`PHONE_MIGRATE_4` on `auth.sendCode`. This is distinct from US-19
(authorization transfer between DCs on an already-authorised
session): PHONE_MIGRATE happens *before* the user is authorised,
during the first interaction from a new client.

## Story
As a new user whose Telegram account lives on a non-default data
centre (common in regions Telegram routes to DC4/DC5 — Iran,
parts of East Asia, etc.), I want my first `tg-cli login` to
discover the correct DC automatically, reconnect there, and
continue the SMS / code / password prompts as if nothing had
happened — no "try again, wrong DC" error, no stack trace.

## Uncovered practical paths
- `auth.sendCode` on DC2 replies with `400 PHONE_MIGRATE_4` →
  client opens DC4 (fresh MTProto handshake), retries
  `auth.sendCode` there, stores DC4 as the new home DC.
- `auth.signIn` on the migrated DC4 completes normally.
- `USER_MIGRATE_5` post-signIn → switch + retry on DC5.
- `NETWORK_MIGRATE_3` appearing mid-session → transient, retry
  the specific RPC on DC3 without reshuffling home DC.
- Ghost migrate loop (server keeps replying `PHONE_MIGRATE_x` after
  the switch): cap the loop at 3 hops, then fail cleanly.

## Acceptance
- Mock server extension: `mt_server_reply_phone_migrate(dc_id)`
  and `mt_server_reply_user_migrate(dc_id)` (one-shot).
- New functional test `tests/functional/test_login_migrate.c`:
  - `test_phone_migrate_first_send_code_switches_home_dc`
  - `test_user_migrate_after_sign_in_switches_home_dc`
  - `test_network_migrate_is_per_rpc_not_home`
  - `test_ghost_migrate_loop_bails_at_3_hops`
- Functional coverage of `auth_session.c` + `mtproto_rpc.c` error
  branches ≥ 85 %.
- Man-page CONFIGURATION / DIAGNOSTICS for tg-cli and tg-tui
  document the migrate-auto-retry behaviour.

## Dependencies
US-03 (login), US-19 (cross-DC auth transfer for authorised
sessions). Shared mock DC infrastructure from TEST-70.
