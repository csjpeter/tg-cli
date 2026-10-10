/**
 * @file pty_tel_stub.h
 * @brief Minimal TCP MTProto stub server for PTY-based functional tests.
 *
 * Binds to 127.0.0.1:0 (OS-assigned port), writes a session.bin with the
 * matching auth_key, and handles one client connection in a background thread.
 *
 * This is intentionally minimal: it responds to messages.getDialogs with an
 * empty dialogs list and returns rpc_error for any other RPC so that the client
 * (tg-tui) never hangs on recv.
 *
 * Usage:
 *   PtyTelStub stub;
 *   pty_tel_stub_start(&stub);     // binds + seeds session + starts thread
 *   setenv("TG_CLI_DC_HOST", "127.0.0.1", 1);
 *   char port_str[16];
 *   snprintf(port_str, sizeof(port_str), "%d", stub.port);
 *   setenv("TG_CLI_DC_PORT", port_str, 1);
 *   // ... run tg-tui via pty_run ...
 *   pty_tel_stub_stop(&stub);
 */

#ifndef PTY_TEL_STUB_H
#define PTY_TEL_STUB_H

#include <stdint.h>
#include <pthread.h>

#define PTY_STUB_AUTH_KEY_SIZE 256

typedef struct {
    int         listen_fd;
    int         port;
    pthread_t   thread;
    int         running;

    uint8_t     auth_key[PTY_STUB_AUTH_KEY_SIZE];
    uint64_t    auth_key_id;
    uint64_t    server_salt;
    uint64_t    session_id;
} PtyTelStub;

/**
 * @brief Initialises the stub: binds a listen socket, seeds session.bin,
 *        and starts the server thread.
 * @return 0 on success, -1 on error.
 */
int pty_tel_stub_start(PtyTelStub *s);

/**
 * @brief Signals the server thread to stop and joins it.
 */
void pty_tel_stub_stop(PtyTelStub *s);

#endif /* PTY_TEL_STUB_H */
