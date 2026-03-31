# msg_container parse

## Description
The server may pack multiple messages into a single `msg_container` (0x73f1f8dc). Each element: msg_id(8) + seqno(4) + bytes(4) + body.

## Estimate
~80 lines

## Dependencies
None
