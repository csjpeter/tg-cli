# FEAT-30 — Support Obfuscated2 / MTProxy transport

## Gap
Users in restrictive networks cannot use tg-cli because raw TCP to
DC IPs is frequently blocked or DPI'd.  Reference clients (official,
tdlib) support:

- MTProto proxy (MTProxy): SOCKS5-like with a secret.
- Obfuscated2 transport: XOR scrambling of the first 64 bytes to
  defeat simple DPI signatures.

Without these, the project is unusable in ~20% of Telegram's user
base.

## Scope
1. Spec: pick Obfuscated2 first (lowest complexity, widely supported).
   Reference: https://core.telegram.org/mtproto/mtproto-transports#transport-obfuscation
2. Add `src/infrastructure/transport_obf2.c` implementing the
   init/read/write wrappers.
3. Arg: `--mtproxy host:port:secret` → reuses infra but through a
   proxy intermediate.
4. Add FT coverage:
   - `test_transport_obf2_handshake` — verifies XOR parameters match
     reference vectors.
   - `test_mtproto_proxy_e2e` — mock proxy server forwards to mock
     Telegram server; full login through the chain.

## Acceptance
- A user can pass `--obfuscated` and connect through a DPI-filtering
  network emulated by an `iptables` rule dropping plain CRCs.
- Man pages document the new flags + protocol security notes.
- SEC-05 is closed by this work (or updated).

## Dependencies
- SEC-05 (doc ticket should land first to set expectations).
