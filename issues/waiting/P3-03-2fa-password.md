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
P3-02, crypto.h (needs SHA-512, PBKDF2)
