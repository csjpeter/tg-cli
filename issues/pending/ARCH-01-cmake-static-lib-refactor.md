# CMake refactor: extract tg-proto static library target

## Description
The main executable (`CMakeLists.txt` lines 44-53) links only 6 source files,
while `tests/unit/CMakeLists.txt` links 26 files with 20+ duplicated
`../../src/...` paths. The entire protocol stack (tl_serial, ige_aes,
mtproto_crypto, mtproto_session, mtproto_auth, transport, mtproto_rpc,
api_call, tl_registry) is compiled only for tests but not for the main binary.

As P5 approaches (user-facing features), the main binary will need all protocol
sources. A shared static library target eliminates duplication and prepares
for integration.

## Steps
1. Create `add_library(tg-proto STATIC ...)` in root CMakeLists.txt with all
   protocol sources (core + infrastructure + platform + vendor)
2. Link main executable against tg-proto: `target_link_libraries(tg-cli PRIVATE tg-proto)`
3. Refactor tests/unit/CMakeLists.txt to link against tg-proto instead of
   listing individual source files (tests still link mock sources separately)
4. Add `posix/socket.c` (or `windows/socket.c`) to PLATFORM_SOURCES since
   transport.c depends on it
5. Verify: `./manage.sh build && ./manage.sh test && ./manage.sh valgrind`

## Estimate
~50 lines (CMake changes only)

## Dependencies
None
