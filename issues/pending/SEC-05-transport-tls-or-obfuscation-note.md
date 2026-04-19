# SEC-05 — Document that MTProto transport is plaintext-on-wire

## Gap
The project uses raw TCP (no TLS, no Obfuscated2) for MTProto.
Encryption is applied at the MTProto 2.0 layer (AES-IGE + auth_key),
but:

1. The initial auth-key DH handshake bytes are visible to any
   passive observer — the first packet reveals this is a Telegram
   client.
2. A DPI / firewall can positively identify tg-cli traffic just
   from the well-known CRCs `req_pq_multi`, `req_DH_params`, etc.
3. Users in restricted networks (the typical Telegram use case)
   expect either MTProxy, Obfuscated2, or WebSocket transport.

The man pages SECURITY sections currently do not mention this.

## Scope
1. Update `.SH SECURITY` in all three man pages to include:
   - "tg-cli uses unauthenticated, unencrypted TCP.  MTProto 2.0
     encryption protects message contents after auth-key
     establishment, but the initial DH handshake is observable.
     The binary does NOT implement MTProxy / Obfuscated2 / WebSocket
     transport.  Users in restricted networks should route traffic
     through a trusted VPN/SSH tunnel."
2. Add a follow-up FEAT ticket for Obfuscated2 support (separate
   ticket, not in this scope).

## Acceptance
- All three man pages clearly state the transport layer contract.
- `docs/dev/mtproto-reference.md` gets a matching note.
- README mentions the limitation prominently.
