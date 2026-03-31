# Media info display

## Description
Recognize MessageMedia objects in messages.getHistory responses and display file name, size, type in history output.

## Types
- messageMediaPhoto → "[Photo: {size}]"
- messageMediaDocument → "[File: {filename} ({size})]"
- messageMediaGeo → "[Location: {lat},{lon}]"
- messageMediaContact → "[Contact: {name} {phone}]"
- messageMediaWebPage → "[Link: {url}]"

## Estimate
~100 lines

## Dependencies
- P5-02 (pending) — messages.getHistory válasz parszolása (MessageMedia objektumok)
