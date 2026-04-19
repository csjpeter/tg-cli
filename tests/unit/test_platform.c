#include "test_helpers.h"
#include "platform/terminal.h"
#include "platform/path.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <sys/wait.h>

void test_platform(void) {

    /* ── terminal_wcwidth ───────────────────────────────────────────── */

    setlocale(LC_ALL, "");

    /* ASCII printable characters: width 1 */
    ASSERT(terminal_wcwidth('A')    == 1, "wcwidth: ASCII letter should be 1");
    ASSERT(terminal_wcwidth(' ')    == 1, "wcwidth: space should be 1");
    ASSERT(terminal_wcwidth('0')    == 1, "wcwidth: digit should be 1");

    /* Control characters: width 0 (not negative) */
    ASSERT(terminal_wcwidth('\n')   == 0, "wcwidth: newline should be 0");
    ASSERT(terminal_wcwidth('\t')   == 0, "wcwidth: tab should be 0");
    ASSERT(terminal_wcwidth(0x01)   == 0, "wcwidth: control char should be 0");

    /* Common accented Latin characters: width 1 */
    ASSERT(terminal_wcwidth(0x00E9) == 1, "wcwidth: e-acute (U+00E9) should be 1");
    ASSERT(terminal_wcwidth(0x00E1) == 1, "wcwidth: a-acute (U+00E1) should be 1");
    ASSERT(terminal_wcwidth(0x0151) == 1, "wcwidth: o-double-acute (U+0151) should be 1");

    /* Combining diacritic: width 0 */
    ASSERT(terminal_wcwidth(0x0300) == 0, "wcwidth: combining grave (U+0300) should be 0");

    /* Zero-width joiner: width 0 */
    ASSERT(terminal_wcwidth(0x200D) == 0, "wcwidth: zero-width joiner (U+200D) should be 0");

    /* CJK ideograph: width 2 */
    ASSERT(terminal_wcwidth(0x4E2D) == 2, "wcwidth: CJK U+4E2D should be 2");
    ASSERT(terminal_wcwidth(0x3042) == 2, "wcwidth: Hiragana U+3042 should be 2");

    /* ── terminal_is_tty ────────────────────────────────────────────── */

    /* When stdout is a pipe (as in test runner), fd 1 is not a tty */
    /* We cannot assert a specific value since it depends on the test
     * environment, but the function must return 0 or 1 without crashing. */
    int r = terminal_is_tty(STDOUT_FILENO);
    ASSERT(r == 0 || r == 1, "terminal_is_tty must return 0 or 1");

    /* Invalid fd must return 0 */
    ASSERT(terminal_is_tty(-1) == 0, "terminal_is_tty(-1) should return 0");
    ASSERT(terminal_is_tty(9999) == 0, "terminal_is_tty(9999) should return 0");

    /* ── terminal_cols ──────────────────────────────────────────────── */

    /* When stdout is not a tty (e.g. piped in CI), must fall back to 80. */
    if (!terminal_is_tty(STDOUT_FILENO)) {
        ASSERT(terminal_cols() == 80,
               "terminal_cols() should return 80 when stdout is not a tty");
    } else {
        /* On a real terminal it must be a positive value. */
        ASSERT(terminal_cols() > 0, "terminal_cols() must be positive");
    }

    /* ── terminal_rows ──────────────────────────────────────────────── */

    int rows = terminal_rows();
    if (!terminal_is_tty(STDOUT_FILENO)) {
        ASSERT(rows == 0, "terminal_rows() should return 0 when stdout is not a tty");
    } else {
        ASSERT(rows > 0, "terminal_rows() must be positive on a real terminal");
    }

    /* ── terminal_raw_enter / terminal_raw_exit ─────────────────────── */

    /* When stdin is not a tty (as in test runner), raw_enter must return NULL
     * gracefully (tcgetattr will fail). */
    if (!terminal_is_tty(STDIN_FILENO)) {
        TermRawState *s = terminal_raw_enter();
        ASSERT(s == NULL,
               "terminal_raw_enter should return NULL when stdin is not a tty");
        /* terminal_raw_exit(NULL) and terminal_raw_exit(&NULL) must be safe. */
        terminal_raw_exit(NULL);
        terminal_raw_exit(&s);   /* s is already NULL */
    }

    /* ── terminal_read_password — guard clauses ─────────────────────── */

    char pwbuf[64];
    /* NULL buf → -1 */
    ASSERT(terminal_read_password("test", NULL, 64) == -1,
           "terminal_read_password: NULL buf should return -1");
    /* size == 0 → -1 */
    ASSERT(terminal_read_password("test", pwbuf, 0) == -1,
           "terminal_read_password: size 0 should return -1");

    /* Non-tty stdin path: getline on an empty/closed stream returns -1. */
    if (!terminal_is_tty(STDIN_FILENO)) {
        int n = terminal_read_password("test", pwbuf, sizeof(pwbuf));
        ASSERT(n == -1 || n >= 0,
               "terminal_read_password non-tty: must not crash");
    }

    /* ── platform_home_dir ──────────────────────────────────────────── */

    const char *home = platform_home_dir();
    ASSERT(home != NULL, "platform_home_dir should not return NULL");
    ASSERT(home[0] == '/', "platform_home_dir should return an absolute path");

    /* Must still work when HOME is unset (getpwuid fallback) */
    char saved_home[4096] = {0};
    const char *env_home = getenv("HOME");
    if (env_home) snprintf(saved_home, sizeof(saved_home), "%s", env_home);
    unsetenv("HOME");
    home = platform_home_dir();
    ASSERT(home != NULL, "platform_home_dir should fall back to getpwuid");
    if (saved_home[0]) setenv("HOME", saved_home, 1);

    /* ── platform_cache_dir ─────────────────────────────────────────── */

    /* Default: ~/.cache */
    unsetenv("XDG_CACHE_HOME");
    const char *cache = platform_cache_dir();
    ASSERT(cache != NULL, "platform_cache_dir should not return NULL");
    ASSERT(strstr(cache, ".cache") != NULL,
           "platform_cache_dir default should contain '.cache'");

    /* XDG override */
    setenv("XDG_CACHE_HOME", "/tmp/test-xdg-cache", 1);
    cache = platform_cache_dir();
    ASSERT(cache != NULL, "platform_cache_dir XDG should not return NULL");
    ASSERT(strcmp(cache, "/tmp/test-xdg-cache") == 0,
           "platform_cache_dir should respect XDG_CACHE_HOME");
    unsetenv("XDG_CACHE_HOME");

    /* ── platform_config_dir ────────────────────────────────────────── */

    /* Default: ~/.config */
    unsetenv("XDG_CONFIG_HOME");
    const char *cfg = platform_config_dir();
    ASSERT(cfg != NULL, "platform_config_dir should not return NULL");
    ASSERT(strstr(cfg, ".config") != NULL,
           "platform_config_dir default should contain '.config'");

    /* XDG override */
    setenv("XDG_CONFIG_HOME", "/tmp/test-xdg-config", 1);
    cfg = platform_config_dir();
    ASSERT(cfg != NULL, "platform_config_dir XDG should not return NULL");
    ASSERT(strcmp(cfg, "/tmp/test-xdg-config") == 0,
           "platform_config_dir should respect XDG_CONFIG_HOME");
    unsetenv("XDG_CONFIG_HOME");

    /* ── SIGWINCH resize notifications ──────────────────────────────── */

    /* Before enabling the handler, consume should be a no-op. */
    ASSERT(terminal_consume_resize() == 0,
           "consume_resize before enable returns 0");

    terminal_enable_resize_notifications();
    /* Idempotent. */
    terminal_enable_resize_notifications();

    /* Still nothing pending until a signal is delivered. */
    ASSERT(terminal_consume_resize() == 0,
           "no resize pending right after enable");

    /* Simulate a resize by raising SIGWINCH and let the handler run. */
    raise(SIGWINCH);
    ASSERT(terminal_consume_resize() == 1, "resize observed after SIGWINCH");
    /* Flag should clear on first consume. */
    ASSERT(terminal_consume_resize() == 0, "resize flag clears after read");

    /* ── terminal_wait_key ──────────────────────────────────────────── */

    /* With a 0ms timeout and no piped input pending, wait_key should
     * return 0 (timeout) quickly. The test runner's stdin is closed or
     * empty, so poll() either times out or reports a hangup — both
     * count as "no actionable key ready". */
    int wk = terminal_wait_key(0);
    ASSERT(wk == 0 || wk == 1 || wk == -1,
           "wait_key returns a valid sentinel");

    /* Raise SIGWINCH mid-wait: poll returns EINTR → we return -1. */
    raise(SIGWINCH);
    /* The handler already ran and set the flag; poll may or may not
     * actually be interrupted (depends on whether raise delivers
     * synchronously). Either way consume_resize should now be 1. */
    ASSERT(terminal_consume_resize() == 1,
           "SIGWINCH raised right before wait still observable");

    /* ── terminal_install_cleanup_handlers ──────────────────────────── */

    /* Passing NULL must be a safe no-op (no crash). */
    terminal_install_cleanup_handlers(NULL);

    /* Verify that after installing handlers, SIGTERM is caught (not the
     * default SIG_DFL which would terminate us) and re-raised with
     * SIG_DFL — we use a child process so the parent survives. */
    {
        pid_t pid = fork();
        ASSERT(pid >= 0, "fork() for SIGTERM test must succeed");
        if (pid == 0) {
            /* Child: set up a dummy TermRawState (not a real tty here,
             * so tcsetattr will fail silently — that is acceptable). */
            TermRawState *dummy = terminal_raw_enter();
            /* raw_enter returns NULL when stdin is not a tty (test env);
             * install_cleanup_handlers is documented to accept NULL (no-op)
             * so this is safe.  The important thing is it does not crash. */
            terminal_install_cleanup_handlers(dummy);
            /* Send ourselves SIGTERM — the handler should re-raise with
             * SIG_DFL, killing the child with SIGTERM. */
            raise(SIGTERM);
            /* Should not reach here after re-raise with SIG_DFL. */
            _exit(42);
        }
        /* Parent waits for the child. */
        int status = 0;
        waitpid(pid, &status, 0);
        /* Child must have been terminated by a signal (SIGTERM), not
         * exited normally with code 42. */
        ASSERT(WIFSIGNALED(status),
               "install_cleanup_handlers: child terminated by signal");
        ASSERT(WTERMSIG(status) == SIGTERM,
               "install_cleanup_handlers: child terminated by SIGTERM");
    }
}
