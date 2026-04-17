#ifndef PLATFORM_TERMINAL_H
#define PLATFORM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

/** Logical key codes returned by terminal_read_key(). */
typedef enum {
    TERM_KEY_QUIT      = 0,  /* Ctrl-C                          */
    TERM_KEY_NEXT_PAGE = 1,  /* PgDn                            */
    TERM_KEY_PREV_PAGE = 2,  /* PgUp                            */
    TERM_KEY_NEXT_LINE = 3,  /* Down-arrow                      */
    TERM_KEY_PREV_LINE = 4,  /* Up-arrow                        */
    TERM_KEY_IGNORE    = 5,  /* unknown / Space                 */
    TERM_KEY_ENTER     = 6,  /* Enter (\n or \r)                */
    TERM_KEY_ESC       = 7,  /* bare ESC                        */
    TERM_KEY_BACK      = 8,  /* Backspace / DEL                 */
    TERM_KEY_LEFT      = 9,  /* Left arrow                      */
    TERM_KEY_RIGHT     = 10, /* Right arrow                     */
    TERM_KEY_HOME      = 11, /* Home key                        */
    TERM_KEY_END       = 12, /* End key                         */
    TERM_KEY_DELETE    = 13, /* Delete (forward delete)         */
    TERM_KEY_CTRL_A    = 14, /* Ctrl-A (beginning of line)      */
    TERM_KEY_CTRL_E    = 15, /* Ctrl-E (end of line)            */
    TERM_KEY_CTRL_K    = 16, /* Ctrl-K (kill to end of line)    */
    TERM_KEY_CTRL_W    = 17, /* Ctrl-W (delete previous word)   */
    TERM_KEY_CTRL_D    = 18, /* Ctrl-D (EOF / delete forward)   */
} TermKey;

/** Opaque saved terminal state (used for raw-mode enter/exit). */
typedef struct TermRawState TermRawState;

/** Returns the terminal width in columns, or 80 if unknown. */
int terminal_cols(void);

/** Returns the terminal height in rows, or 0 if unknown. */
int terminal_rows(void);

/** Returns 1 if fd is connected to a terminal, 0 otherwise. */
int terminal_is_tty(int fd);

/**
 * Save current terminal mode and enter raw mode
 * (no echo, no canonical, no signal generation).
 * Returns an allocated TermRawState on success, NULL on failure.
 * Caller must call terminal_raw_exit() to restore and free.
 */
TermRawState *terminal_raw_enter(void);

/**
 * Restore the terminal to the state saved in *state and free it.
 * Sets *state = NULL.  Safe to call with NULL or *state == NULL.
 */
void terminal_raw_exit(TermRawState **state);

/** RAII cleanup wrapper for terminal_raw_exit. */
static inline void terminal_raw_exit_ptr(TermRawState **p) {
    terminal_raw_exit(p);
}
#define RAII_TERM_RAW __attribute__((cleanup(terminal_raw_exit_ptr)))

/**
 * Read one keypress and return a TermKey code.
 * The terminal must already be in raw mode.
 * Fully consumes multi-byte escape sequences.
 */
TermKey terminal_read_key(void);

/**
 * Wait up to @p timeout_ms for a keystroke to be ready on stdin.
 * Returns 1 when at least one byte is pending (read_key will not
 * block for that first byte), 0 on timeout, -1 on interrupt or
 * error. The terminal need not be in raw mode. Pass a negative
 * timeout to block indefinitely.
 */
int     terminal_wait_key(int timeout_ms);

/**
 * Returns the last printable ASCII character (32–126) that caused
 * terminal_read_key() to return TERM_KEY_IGNORE.
 * Returns 0 if the last ignored keystroke was not a printable character.
 */
int terminal_last_printable(void);

/**
 * Display column width of Unicode codepoint cp.
 * Returns 0 for non-printable/control characters,
 * 1 for normal characters, 2 for wide (CJK/emoji) characters.
 */
int terminal_wcwidth(uint32_t cp);

/**
 * Prompt for a password with echo suppressed.
 * Writes at most size-1 bytes to buf (NUL-terminated).
 * Returns the number of characters read, or -1 on error.
 */
int terminal_read_password(const char *prompt, char *buf, size_t size);

/**
 * Enable terminal-resize notifications. On POSIX this installs a
 * SIGWINCH handler that flips an internal flag; on Windows this is a
 * no-op (resize is picked up on the next read via a different path).
 * Safe to call multiple times.
 */
void terminal_enable_resize_notifications(void);

/**
 * If a terminal-resize event has occurred since the last call, clear
 * the pending flag and return 1; otherwise return 0. Used by TUI
 * loops to detect SIGWINCH between reads.
 */
int  terminal_consume_resize(void);

#endif /* PLATFORM_TERMINAL_H */
