# FEAT-21: Log file created with default umask — should be 0600

## Location
`src/core/logger.c` – `logger_init()`

## Problem
`logger_init()` opens the log file with `fopen(g_log_path, "a")`.  The file is
created with the process's default umask (typically 0022 → mode 0644), making
it world-readable.

Log files can contain:
- Phone number fragments (see SEC-02)
- `bad_server_salt` retry messages containing the new salt value
- `auth: DH key exchange complete` milestones that mark active sessions
- At `LOG_DEBUG`: `msg_id`, `seq_no`, `salt`, `session_id`

`session.bin` is explicitly set to 0600; the log file should be too.

## Fix direction
After the `fopen` call, apply `fs_ensure_permissions(g_log_path, 0600)`.
Alternatively, use `open(O_CREAT|O_WRONLY|O_APPEND, 0600)` and wrap in
`fdopen(fd, "a")` so the mode is set atomically on creation.

## Test
`tests/unit/test_logger.c`: after `logger_init()`, `stat()` the log file and
assert `st.st_mode & 0777 == 0600`.
