# TEST-33: transport send/recv loops do not retry on EINTR

## Location
`src/infrastructure/transport.c` – `transport_send()` / `transport_recv()`
`src/platform/posix/socket.c` – `sys_socket_send()` / `sys_socket_recv()`

## Problem
`sys_socket_send` and `sys_socket_recv` are thin wrappers around `send(2)` /
`recv(2)` with no EINTR retry:
```c
ssize_t sys_socket_send(int fd, const void *buf, size_t len) {
    return send(fd, buf, len, 0);
}
```

When a signal arrives (e.g. `SIGWINCH` in tg-tui, `SIGCHLD`) while the socket
is blocking, the syscall returns -1 with `errno == EINTR`.  `transport_send`'s
inner loop checks `sent <= 0` and logs an error, treating EINTR as a fatal
connection failure.  This can cause spurious disconnects in the TUI.

The platform terminal layer explicitly disables `SA_RESTART` for SIGWINCH
("we want blocking read(2) to return with EINTR") — which makes it even more
likely that socket syscalls in the same process are interrupted.

## Fix direction
Retry on EINTR in `sys_socket_send` / `sys_socket_recv`, or equivalently in the
`transport_send` / `transport_recv` inner loops:
```c
do { sent = sys_socket_send(fd, ...); } while (sent < 0 && errno == EINTR);
```

## Test
Mock test: make `sys_socket_send` return -1/EINTR on first call, success on
second; assert `transport_send` returns 0 (not -1).
