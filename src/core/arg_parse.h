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
 *   if (rc == ARG_HELP)    { arg_print_help();    return 0; }
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
} ArgCommand;

/** Parsed argument result. All string pointers point into argv (no copy). */
typedef struct {
    /* Global flags */
    int         batch;       /**< --batch : non-interactive mode.        */
    int         json;        /**< --json  : machine-readable JSON output. */
    int         quiet;       /**< --quiet : suppress informational output. */
    const char *config_path; /**< --config <path> : custom config file.  */

    /* Subcommand */
    ArgCommand  command;

    /* Per-subcommand arguments */
    const char *peer;        /**< Peer identifier (username / id).       */
    const char *message;     /**< Message text (send).                   */
    const char *query;       /**< Search query (search).                 */
    int         limit;       /**< --limit N (dialogs, history).          */
    int         offset;      /**< --offset N (history).                  */
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
 * @brief Print help text to stdout. */
void arg_print_help(void);

/**
 * @brief Print version string to stdout. */
void arg_print_version(void);

#endif /* ARG_PARSE_H */
