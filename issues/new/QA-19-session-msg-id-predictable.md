# mtproto_session uses rand() for msg_id generation

## Description
`mtproto_session_next_msg_id()` in `src/core/mtproto_session.c` uses
`rand()` (libc PRNG) to generate parts of the msg_id. `rand()` is seeded
with `srand(time(NULL))` which is trivially predictable.

The MTProto spec requires msg_id to be approximately equal to
`unixtime * 2^32` with the lower bits ensuring uniqueness. While
predictability of msg_id is not a direct crypto vulnerability, it
enables:
- Replay attack detection bypass (if an attacker can predict future msg_ids)
- Server-side rate limiting evasion

## Severity
MEDIUM — weak randomness in protocol-critical identifier.

## Steps
1. Replace `rand()` with `crypto_rand_bytes()` for the non-time portion
2. Ensure msg_id formula follows spec: `(uint64_t)time(NULL) * (1ULL << 32) | (random & 0xFFFFFFFC)`
3. Add test verifying monotonicity and non-predictability

## Estimate
~10 lines

## Dependencies
None
