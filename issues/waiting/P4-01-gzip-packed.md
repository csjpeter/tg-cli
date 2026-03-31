# gzip_packed handling

## Description
Telegram server wraps large responses in `gzip_packed` (0x3072cfa1) constructor. The RPC layer must detect this and decompress using tinf.

## Steps
1. After `rpc_recv`, check if first 4 bytes of TL payload == 0x3072cfa1
2. Read the compressed bytes field
3. Decompress using `tinf_gzip_uncompress()`
4. Continue TL parsing with decompressed data

## Estimate
~50 lines

## Dependencies
tinf (already integrated)
