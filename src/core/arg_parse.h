/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file arg_parse.h
 * @brief Custom command-line argument parser for tg-cli.
 *
 * Supports global flags and subcommands with per-subcommand arguments.
 * No external dependencies — pure C11 / stdlib.
 *
 * Usage:
 *   ArgResult args;
 *   int rc = arg_parse(argc, argv, &args);
 *   if (rc == ARG_HELP)    { print_usage(); return 0; }  // per-binary custom help
 *   if (rc == ARG_VERSION) { arg_print_version(); return 0; }
 *   if (rc < 0)            { return 1; }
 */

#ifndef ARG_PARSE_H
#define ARG_PARSE_H

/** Subcommand identifiers. */
typedef enum {
    CMD_NONE      = 0,
    CMD_DIALOGS,      /**< List dialogs/chats.                         */
    CMD_HISTORY,      /**< Fetch message history for a peer.           */
    CMD_SEND,         /**< Send a message to a peer.                   */
    CMD_SEARCH,       /**< Search messages (optionally within a peer). */
    CMD_CONTACTS,     /**< List contacts.                              */
    CMD_USER_INFO,    /**< Show info for a user/channel.               */
    CMD_ME,           /**< Show own profile (US-05).                   */
    CMD_WATCH,        /**< Watch incoming updates (US-07).             */
    CMD_DOWNLOAD,     /**< Download media by message id (US-08/P6-01). */
    CMD_READ,         /**< Mark history as read (P5-04).               */
    CMD_EDIT,         /**< Edit an existing message (P5-06).           */
    CMD_DELETE,       /**< Delete one or more messages (P5-06).        */
    CMD_FORWARD,      /**< Forward messages from one peer to another.  */
    CMD_SEND_FILE,    /**< Upload a file as a document (P6-02).        */
} ArgCommand;

/** @brief Extra fields for the P5-06 edit / delete / forward / reply
 *         family of commands. Kept in ArgResult as loose fields rather
 *         than a union to stay consistent with the existing structure. */

/** Parsed argument result. All string pointers point into argv (no copy). */
typedef struct {
    /* Global flags */
    int         batch;       /**< --batch : non-interactive mode.        */
    int         json;        /**< --json  : machine-readable JSON output. */
    int         quiet;       /**< --quiet : suppress informational output. */
    const char *config_path; /**< --config <path> : custom config file.  */

    /* Login credentials (batch mode; NULL otherwise) */
    const char *phone;        /**< --phone +15551234567                   */
    const char *code;         /**< --code 12345                           */
    const char *password;     /**< --password ... (2FA)                   */

    /* Subcommand */
    ArgCommand  command;

    /* Per-subcommand arguments */
    const char *peer;        /**< Peer identifier (username / id).       */
    const char *message;     /**< Message text (send).                   */
    const char *query;       /**< Search query (search).                 */
    int         limit;       /**< --limit N (dialogs, history).          */
    int         archived;    /**< --archived : show archived folder (dialogs). */
    int         offset;      /**< --offset N (history).                  */
    int         msg_id;      /**< Message id for download.               */
    const char *out_path;    /**< --out <path> for download.             */

    /* P5-06 extras */
    const char *peer2;       /**< Destination peer for `forward`.        */
    int         revoke;      /**< --revoke for delete.                   */
    int         reply_to;    /**< --reply <msg_id> for send.             */

    /* Watch extras */
    int         watch_interval; /**< --interval N for watch [2..3600], default 30. */
    const char *watch_peers;    /**< --peers X,Y,Z for watch: comma-separated peer list.
                                  *  Each token may be @username, numeric id, or "self".
                                  *  NULL means "no filter — emit all messages". */

    /* History extras */
    int         no_media; /**< --no-media: suppress pure-media messages; show only caption for mixed. */
} ArgResult;

/**
 * @brief Return codes from arg_parse (non-negative = ok). */
#define ARG_OK       0
#define ARG_HELP     1
#define ARG_VERSION  2
#define ARG_ERROR   -1

/**
 * @brief Parse argc/argv into an ArgResult.
 *
 * @param argc  Argument count from main().
 * @param argv  Argument vector from main().
 * @param out   Output structure; zeroed then filled on success.
 * @return ARG_OK, ARG_HELP, ARG_VERSION, or ARG_ERROR.
 */
int arg_parse(int argc, char **argv, ArgResult *out);

/**
 * @brief Print version string to stdout. */
void arg_print_version(void);

#endif /* ARG_PARSE_H */
