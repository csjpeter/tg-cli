# FEAT-38 — config.ini overrides for DC endpoints and RSA public key

## Problem

`src/app/dc_config.c` hard-codes the five Telegram DC host addresses and
`src/core/telegram_server_key.c` hard-codes the canonical RSA public key.
Telegram's my.telegram.org shows both values to every developer.  When the
values match there is no issue, but there is no mechanism for a user to
override them without recompiling the binary.

## Story

As a user whose Telegram account is routed through non-standard DCs or who
has an API application configured with a non-default key (test environment,
regional DC, future key rotation), I want to override DC hostnames and the
RSA public key in `~/.config/tg-cli/config.ini` without modifying source
code or build artifacts.

## Acceptance

- `config.ini` accepts the following optional keys:

  ```ini
  rsa_pem   = -----BEGIN RSA PUBLIC KEY-----\n...\n-----END RSA PUBLIC KEY-----
  dc_1_host = 149.154.175.50
  dc_2_host = 149.154.167.50
  dc_3_host = 149.154.175.100
  dc_4_host = 149.154.167.91
  dc_5_host = 91.108.56.130
  ```

- When a key is present, `dc_config_lookup()` prefers the configured value
  over the built-in table.
- When `rsa_pem` is present, `mtproto_auth_key_gen()` uses the configured
  key instead of `telegram_server_key.c`.
- Unset keys fall back to compiled-in defaults (backwards-compatible).
- Functional tests cover: config-supplied DC host used for connection;
  config-supplied RSA PEM used for DH; fallback to built-in when absent.
- `./manage.sh test` green, ASAN + Valgrind clean.

## Dependencies

US-20 (fresh-install handshake), DOC-34 (setup guide — already updated to
reference this ticket).
