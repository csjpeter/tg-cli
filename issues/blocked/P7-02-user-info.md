# users.getFullUser / channels.getFullChannel (profile data)

## Batch
- `tg-cli user-info <user_id|@username>`
- `tg-cli channel-info <id|@name>`

## Output
id | name | username | bio | phone | photo_url | members_count | ...

## Estimate
~150 lines

## Dependencies
- P7-01 (pending) — contacts.getContacts (User objektumok)
- P7-03 (pending, soft) — @username feloldás user_id-re
- P3-02 (pending) — bejelentkezés szükséges
