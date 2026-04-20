# TEST-86 — Functional coverage for PHONE_MIGRATE / USER_MIGRATE during login

## Gap
`rpc_parse_error` handles the migrate variants, but no functional
test drives a login in which `auth.sendCode` or `auth.signIn`
returns `PHONE_MIGRATE_x` / `USER_MIGRATE_x`. Users in regions
where Telegram routes accounts to DC4/DC5 hit this on every first
install.

## Scope
Mock-server fixtures (building on TEST-70):
- `mt_server_reply_phone_migrate(dc_id)` — next `auth.sendCode`
  responds with `400 PHONE_MIGRATE_<dc_id>`.
- `mt_server_reply_user_migrate(dc_id)` — same for `auth.signIn`.
- `mt_server_reply_network_migrate(dc_id)` — per-RPC transient.

New suite `tests/functional/test_login_migrate.c`:

1. `test_phone_migrate_first_send_code_switches_home_dc` —
   bootstrap on DC2, sendCode on DC2 replies PHONE_MIGRATE_4, retry
   on DC4 succeeds, session.bin home_dc becomes 4.
2. `test_user_migrate_after_sign_in_switches_home_dc`.
3. `test_network_migrate_is_per_rpc_not_home` — only the failing
   RPC retries on DC3; home DC stays unchanged.
4. `test_ghost_migrate_loop_bails_at_3_hops` — server keeps
   replying PHONE_MIGRATE after switches; client gives up with a
   clear error after 3 hops.

## Acceptance
- 4 tests pass under ASAN + Valgrind.
- Functional coverage of `auth_session.c` ≥ 80 % and
  `mtproto_rpc.c` error-code branches ≥ 90 %.

## Dependencies
US-35 (the story). TEST-70 / TEST-71 (multi-DC mock scaffolding).
