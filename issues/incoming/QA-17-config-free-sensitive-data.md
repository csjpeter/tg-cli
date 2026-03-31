# config_free() does not scrub sensitive data before freeing

## Description
`config_free()` in `src/core/config.c` frees `cfg->token` and `cfg->api_base`
without zeroing the memory first. The Telegram API token is a credential —
after `free()`, the token string remains in the heap and can be recovered by
memory inspection (core dump, `/proc/pid/mem`, heap spray).

## Severity
HIGH — credential exposure in post-mortem memory analysis.

## Steps
1. Before `free(cfg->token)`: `if (cfg->token) explicit_bzero(cfg->token, strlen(cfg->token));`
2. Same for `cfg->api_base` if it contains sensitive DC connection info
3. Include `<string.h>` for `explicit_bzero` (or POSIX `memset_s` fallback)

## Estimate
~5 lines

## Dependencies
None
