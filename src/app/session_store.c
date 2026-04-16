/**
 * @file app/session_store.c
 * @brief Simple binary file with the persisted session state.
 */

#include "app/session_store.h"

#include "fs_util.h"
#include "logger.h"
#include "platform/path.h"
#include "raii.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_MAGIC   "TGCS"
#define STORE_VERSION 1
#define STORE_SIZE    284  /* 4+4+4+8+8+256 */

static char *store_path(void) {
    const char *cfg = platform_config_dir();
    if (!cfg) return NULL;
    char *p = NULL;
    if (asprintf(&p, "%s/tg-cli/session.bin", cfg) == -1) return NULL;
    return p;
}

int session_store_save(const MtProtoSession *s, int dc_id) {
    if (!s || !s->has_auth_key) return -1;

    const char *cfg_dir = platform_config_dir();
    if (!cfg_dir) return -1;

    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/tg-cli", cfg_dir);
    if (fs_mkdir_p(dir_path, 0700) != 0) {
        logger_log(LOG_ERROR, "session_store: cannot create %s", dir_path);
        return -1;
    }

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    RAII_FILE FILE *f = fopen(path, "wb");
    if (!f) {
        logger_log(LOG_ERROR, "session_store: cannot open %s for writing", path);
        return -1;
    }

    uint8_t buf[STORE_SIZE] = {0};
    memcpy(buf, STORE_MAGIC, 4);
    int32_t version = STORE_VERSION;
    memcpy(buf + 4, &version, 4);
    int32_t dc = dc_id;
    memcpy(buf + 8,  &dc, 4);
    memcpy(buf + 12, &s->server_salt, 8);
    memcpy(buf + 20, &s->session_id, 8);
    memcpy(buf + 28, s->auth_key, 256);

    size_t n = fwrite(buf, 1, STORE_SIZE, f);
    if (n != STORE_SIZE) {
        logger_log(LOG_ERROR, "session_store: short write to %s", path);
        return -1;
    }

    fflush(f);
    if (fs_ensure_permissions(path, 0600) != 0) {
        logger_log(LOG_WARN, "session_store: cannot set 0600 on %s", path);
    }
    logger_log(LOG_INFO, "session_store: persisted session (DC%d)", dc_id);
    return 0;
}

int session_store_load(MtProtoSession *s, int *dc_id) {
    if (!s || !dc_id) return -1;

    RAII_STRING char *path = store_path();
    if (!path) return -1;

    RAII_FILE FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint8_t buf[STORE_SIZE];
    if (fread(buf, 1, STORE_SIZE, f) != STORE_SIZE) {
        logger_log(LOG_WARN, "session_store: truncated file");
        return -1;
    }
    if (memcmp(buf, STORE_MAGIC, 4) != 0) {
        logger_log(LOG_WARN, "session_store: bad magic");
        return -1;
    }
    int32_t version;
    memcpy(&version, buf + 4, 4);
    if (version != STORE_VERSION) {
        logger_log(LOG_WARN, "session_store: unsupported version %d", version);
        return -1;
    }

    int32_t dc;
    memcpy(&dc, buf + 8, 4);
    *dc_id = dc;
    memcpy(&s->server_salt, buf + 12, 8);
    memcpy(&s->session_id,  buf + 20, 8);
    memcpy(s->auth_key,     buf + 28, 256);
    s->has_auth_key = 1;
    s->seq_no = 0;
    s->last_msg_id = 0;

    logger_log(LOG_INFO, "session_store: loaded session (DC%d)", dc);
    return 0;
}

void session_store_clear(void) {
    RAII_STRING char *path = store_path();
    if (!path) return;
    if (remove(path) == 0)
        logger_log(LOG_INFO, "session_store: cleared");
}
