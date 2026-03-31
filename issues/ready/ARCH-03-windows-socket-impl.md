# Windows socket.c implementation

## Description
`src/platform/windows/` contains `terminal.c` and `path.c` but is missing
`socket.c`. The `socket.h` header declares 6 functions (`sys_socket_create`,
`sys_socket_connect`, `sys_socket_send`, `sys_socket_recv`, `sys_socket_close`,
`sys_socket_set_nonblocking`) that have a POSIX implementation in
`src/platform/posix/socket.c` but no Windows counterpart.

When `transport.c` is linked into the main binary, Windows builds will fail
with unresolved symbols.

## Steps
1. Create `src/platform/windows/socket.c` implementing all functions from
   `src/platform/socket.h` using Winsock2 API:
   - `sys_socket_create` → `WSAStartup` + `socket()`
   - `sys_socket_connect` → `getaddrinfo` + `connect()`
   - `sys_socket_send` → `send()`
   - `sys_socket_recv` → `recv()`
   - `sys_socket_close` → `closesocket()` + `WSACleanup()`
   - `sys_socket_set_nonblocking` → `ioctlsocket(FIONBIO)`
2. Add `src/platform/windows/socket.c` to WIN32 PLATFORM_SOURCES in CMakeLists.txt
3. Add `src/platform/posix/socket.c` to else PLATFORM_SOURCES in CMakeLists.txt
4. Update portability table in CLAUDE.md with Winsock2 note

## Estimate
~80 lines

## Dependencies
- ARCH-01 (pending, soft) — CMake refactor tisztábbá teszi a source hozzáadást, de nem blokkoló
