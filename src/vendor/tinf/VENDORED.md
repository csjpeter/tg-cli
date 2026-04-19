# Vendored tinf Library

## Overview
This directory contains a vendored copy of **tinf** — a tiny inflate library for decompressing deflate, gzip, and zlib streams.

## Upstream Information
- **Upstream Repository:** https://github.com/jibsen/tinf
- **License:** Zlib (see LICENSE file in this directory)
- **Copyright:** 2003-2019 Joergen Ibsen

## Version Pinned
- **Version:** 1.2.1
- **Date Vendored:** 2026-04-19
- **Last Audited:** 2026-04-19

## Patches Applied
No patches have been applied to this version. The code is used as-is from upstream.

## Security Notes
- **CVE-2018-14556** (heap overflow in tinf versions prior to 1.2.0) is fixed in this version (1.2.1).
- No known CVEs affecting tinf 1.2.1 as of the audit date.

## Usage
The library provides three public decompression APIs:
- `tinf_uncompress()` — raw deflate decompression
- `tinf_gzip_uncompress()` — gzip stream decompression
- `tinf_zlib_uncompress()` — zlib stream decompression

Checksum utilities: `tinf_adler32()`, `tinf_crc32()`.

See `tinf.h` for the complete public API.
