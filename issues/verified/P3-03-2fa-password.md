# 2FA Password (auth.checkPassword + SRP)

## Description
If the user has two-step verification enabled, `auth.signIn` returns `SESSION_PASSWORD_NEEDED`. The password must be verified using the SRP protocol.

## API
- `account.getPassword` → SRP parameters (srp_id, B, current_algo)
- `auth.checkPassword(inputCheckPasswordSRP)` → `auth.authorization`

## SRP Steps
1. Extract salt1, salt2, g, p from current_algo
2. x = PH2(password, salt1, salt2) — PBKDF2-HMAC-SHA512
3. v = pow(g, x) mod p
4. k = H(p, g)
5. a = random 256 bytes, A = pow(g, a) mod p
6. u = H(A, B)
7. S = pow(B - k*v, a + u*x) mod p
8. K = H(S)
9. M1 = H(H(p) XOR H(g), H(salt1), H(salt2), A, B, K)

## Estimate
~400 lines (SRP is complex)

## Dependencies
- P3-02 (pending) — auth.signIn kell az SESSION_PASSWORD_NEEDED válaszhoz
- crypto.h bővítés szükséges: SHA-512, PBKDF2-HMAC-SHA512 (jelenleg nincs implementálva)

## Completion notes (2026-04-16)
- Phase A: crypto.h/c gained crypto_sha512 + crypto_pbkdf2_hmac_sha512
  (EVP_sha512 / PKCS5_PBKDF2_HMAC). Mock crypto mirrors the surface.
  Functional tests land NIST/FIPS known answers for SHA-512 and the
  standard PBKDF2-SHA512 reference vector (password/salt/c=1).
- Phase B1: crypto.h/c gained crypto_bn_mod_mul, mod_add, mod_sub, ucmp
  (BN_mod_mul / BN_mod_add / BN_mod_sub / BN_ucmp). Mock equivalents
  preserve call-count semantics.
- Phase B2: tl_registry learned account.password, passwordKdfAlgo...,
  inputCheckPasswordSRP, inputCheckPasswordEmpty. Legacy
  TL_auth_password constant replaced with the correct TL_account_password.
- Phase B3: infrastructure/auth_2fa.{h,c} adds Account2faPassword +
  auth_2fa_get_password (RPC + parse) + auth_2fa_check_password
  (SRP compute + RPC). SRP uses the identity
  S = base^a * base^(x*u) mod p to avoid building the full
  (a + u*x) exponent — each mod_exp stays on modulus-sized operands.
  M1 = SHA256(H(p)^H(g) | H(salt1) | H(salt2) | A | B | K), K = SHA256(S).
- Phase B4: auth_flow.c now drives the full SRP flow when the server
  returns SESSION_PASSWORD_NEEDED and a get_password callback is
  available; the session is persisted on success just like the
  no-2FA path.
- Tests: test_auth_2fa.c covers getPassword parse (with/without 2FA),
  RPC error propagation, checkPassword flag-guard, and an end-to-end
  mock run that asserts PBKDF2 is invoked once and mod_exp at least
  three times. Test helper fixed to emit 0x7F + 3-byte LE length
  prefix for frames ≥ 0x7F dwords so larger mock payloads round-trip.
- Total: 1921 unit + 131 functional tests green, valgrind clean.
- Cross-DC + secure_random parse + email recovery remain out of scope
  for P3-03 (read-only MVP). Document + non-photo download not yet.
