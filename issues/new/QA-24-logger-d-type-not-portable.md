# logger_clean_logs uses d_type which is not portable

## Description
`logger_clean_logs()` in `src/core/logger.c` (line 131) checks
`entry->d_type == DT_REG` to identify regular files. The `d_type` field in
`struct dirent` is:

- Not required by POSIX (only guaranteed on Linux ext4, XFS, btrfs, tmpfs)
- Returns `DT_UNKNOWN` on many filesystems (NFS, ext2 without feature flag,
  some FUSE mounts)
- Not available on all platforms (some older BSDs, Android NDK edge cases)

When `d_type == DT_UNKNOWN`, log files are silently skipped and never cleaned.

## Severity
LOW — log cleanup silently fails on some filesystems; no data corruption.

## Steps
1. When `d_type == DT_UNKNOWN`, fall back to `stat()` or `lstat()` to check
   if the entry is a regular file
2. Consider using `stat()` unconditionally for simplicity and portability

## Estimate
~10 lines

## Dependencies
None
