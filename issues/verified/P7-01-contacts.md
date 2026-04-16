# contacts.getContacts (contact list)

## Batch
`tg-cli contacts [--json]`

## Output
user_id | first_name | last_name | username | phone

## Estimate
~100 lines

## Dependencies
- P4-05 ✅ (verified) — constructor registry
- P3-02 (pending) — API hívásokhoz bejelentkezés szükséges

## Verified — 2026-04-16 (v1)
- `src/domain/read/contacts.{h,c}` domain_get_contacts() issues
  contacts.getContacts (hash=0) and iterates Vector<Contact>
  extracting user_id + mutual flag.
- `tg-cli-ro contacts` emits plain or JSON.
- 3 unit tests (simple, rpc_error, null args). 1795 -> 1803.

## Remaining
- Join Contact.user_id with the trailing users:Vector<User> to
  surface display names — blocked on User skipper (P5-07 phase 3).
