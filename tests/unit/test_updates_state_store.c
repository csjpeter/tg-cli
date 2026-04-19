/**
 * @file test_updates_state_store.c
 * @brief Unit tests for updates_state_load / updates_state_save.
 *
 * These tests do NOT go through platform_cache_dir(); instead they exercise
 * the INI read/write logic by writing temporary files directly and calling
 * the public API with a custom HOME so that platform_cache_dir() resolves
 * to a directory we control.
 */

#include "test_helpers.h"
#include "infrastructure/updates_state_store.h"
#include "fs_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ---- Helpers ------------------------------------------------------------ */

/** Set HOME to a writable temp directory and return it (caller must free). */
static char *setup_tmp_home(void) {
    char tmpl[] = "/tmp/tg-cli-state-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return NULL;
    char *copy = strdup(dir);
    setenv("HOME", copy, 1);
    /* Also clear XDG_CACHE_HOME so platform_cache_dir() uses $HOME/.cache */
    unsetenv("XDG_CACHE_HOME");
    return copy;
}

/** Remove directory tree (shallow — we only create one level of files). */
static void rm_rf(const char *dir) {
    /* Use execvp to avoid shell quoting and snprintf size issues. */
    char *args[] = { "rm", "-rf", (char *)dir, NULL };
    pid_t pid = fork();
    if (pid == 0) {
        execvp("rm", args);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* ---- Tests -------------------------------------------------------------- */

/** Happy path: save then load round-trips all fields. */
static void test_save_load_roundtrip(void) {
    char *home = setup_tmp_home();
    ASSERT(home != NULL, "mkdtemp must succeed");

    UpdatesState orig = {
        .pts          = 12345,
        .qts          = 67,
        .date         = 1700000000,
        .seq          = 99,
        .unread_count = 7,
    };

    int rc = updates_state_save(&orig);
    ASSERT(rc == 0, "save must succeed");

    /* Verify the file was created. */
    char path[512];
    snprintf(path, sizeof(path), "%s/.cache/tg-cli/updates.state", home);
    ASSERT(access(path, F_OK) == 0, "state file must exist after save");

    /* Verify mode 0600. */
    struct stat st;
    ASSERT(stat(path, &st) == 0, "stat must succeed");
    ASSERT((st.st_mode & 0777) == 0600, "state file must have mode 0600");

    UpdatesState loaded = {0};
    rc = updates_state_load(&loaded);
    ASSERT(rc == 0, "load must succeed");

    ASSERT(loaded.pts  == orig.pts,  "pts round-trips");
    ASSERT(loaded.qts  == orig.qts,  "qts round-trips");
    ASSERT(loaded.date == orig.date, "date round-trips");
    ASSERT(loaded.seq  == orig.seq,  "seq round-trips");
    ASSERT(loaded.unread_count == orig.unread_count, "unread_count round-trips");

    rm_rf(home);
    free(home);
}

/** Missing file returns -1 (not a hard error). */
static void test_load_missing_file(void) {
    char *home = setup_tmp_home();
    ASSERT(home != NULL, "mkdtemp must succeed");

    /* Do NOT create the state file. */
    UpdatesState out = {0};
    int rc = updates_state_load(&out);
    ASSERT(rc == -1, "load of missing file must return -1");

    rm_rf(home);
    free(home);
}

/** Overwrite: a second save replaces the first. */
static void test_save_overwrites(void) {
    char *home = setup_tmp_home();
    ASSERT(home != NULL, "mkdtemp must succeed");

    UpdatesState first  = { .pts=100, .qts=1, .date=1000, .seq=5 };
    UpdatesState second = { .pts=200, .qts=2, .date=2000, .seq=6 };

    ASSERT(updates_state_save(&first)  == 0, "first save ok");
    ASSERT(updates_state_save(&second) == 0, "second save ok");

    UpdatesState loaded = {0};
    ASSERT(updates_state_load(&loaded) == 0, "load after overwrite ok");
    ASSERT(loaded.pts == 200, "pts reflects second save");
    ASSERT(loaded.seq == 6,   "seq reflects second save");

    rm_rf(home);
    free(home);
}

/** Corrupt file (no '=' separator) returns -2. */
static void test_load_corrupt_file(void) {
    char *home = setup_tmp_home();
    ASSERT(home != NULL, "mkdtemp must succeed");

    /* Create directory and write a bad file. */
    char *dir  = NULL;
    char *path = NULL;
    if (asprintf(&dir,  "%s/.cache/tg-cli",          home) == -1 ||
        asprintf(&path, "%s/.cache/tg-cli/updates.state", home) == -1) {
        free(dir);
        rm_rf(home); free(home);
        return;
    }
    fs_mkdir_p(dir, 0700);
    free(dir);

    FILE *fp = fopen(path, "w");
    free(path);
    ASSERT(fp != NULL, "can create corrupt file");
    fprintf(fp, "this is not valid ini\n");
    fclose(fp);

    UpdatesState out = {0};
    int rc = updates_state_load(&out);
    ASSERT(rc == -2, "corrupt file must return -2");

    rm_rf(home);
    free(home);
}

/* ---- Entry point -------------------------------------------------------- */

void run_updates_state_store_tests(void) {
    RUN_TEST(test_save_load_roundtrip);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_save_overwrites);
    RUN_TEST(test_load_corrupt_file);
}
