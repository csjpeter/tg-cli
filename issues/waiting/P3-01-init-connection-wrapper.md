# initConnection + invokeWithLayer wrapper

## Description
Every Telegram API call must be wrapped in `invokeWithLayer(layer, initConnection(..., query))`. This includes api_id, device model, app version, system language, etc.

## API
- `invokeWithLayer` (TL constructor: 0xda9b0d0d)
- `initConnection` (TL constructor: 0xc1cd5ea9)

## Estimate
~100 lines

## Dependencies
None (but all Phase 5+ API calls depend on this)
