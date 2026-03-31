# TL Constructor Registry

## Description
Constructor ID → handler mapping. TL objects in server responses (User, Message, Chat, etc.) are identified and parsed by constructor ID.

## Approach
Static array: `{ constructor_id, name, parse_func }`. Start with only the required constructors (~30-50).

## Estimate
~300 lines

## Dependencies
None
