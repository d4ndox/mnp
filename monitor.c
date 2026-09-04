/* ============================================================
 * monitor.c
 * ============================================================ */

#include "monitor.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globaldefs.h"

/*
 * Keep monitor-specific options local to this module.
 *
 * Supported:
 *
 *   mnp TXID --notify-at 2 --confirmation 3
 *   echo TXID | mnp --notify-at 2 --confirmation 3
 */
static const struct option monitor_options[] = {
    {"notify-at", required_argument, NULL, 'o'},
    {"confirmation", required_argument, NULL, 'n'},
    {NULL, 0, NULL, 0},
};

struct monitor_args {
    int notify;
    int confirmation;
    const char *txid_argument;
};

static int parse_integer(
    const char *value,
    int minimum,
    int maximum,
    int *result
)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || *value == '\0' || result == NULL) {
        return -1;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);

    if (errno == ERANGE ||
        end == value ||
        *end != '\0' ||
        parsed < minimum ||
        parsed > maximum) {
        return -1;
    }

    *result = (int)parsed;

    return 0;
}

static int parse_monitor_args(
    int argc,
    char **argv,
    struct monitor_args *args
)
{
    int option;

    if (args == NULL) {
        return -1;
    }

    args->notify = CONFIRMED;
    args->confirmation = 0;
    args->txid_argument = NULL;

    /*
     * monitor_main() may be invoked multiple times in tests or from
     * another dispatcher, so getopt state must not leak between calls.
     */
    optind = 1;
    opterr = 0;

    while ((option = getopt_long(
                argc,
                argv,
                "",
                monitor_options,
                NULL
            )) != -1) {
        switch (option) {
        case 'o':
            if (parse_integer(
                    optarg,
                    NONE,
                    UNLOCKED,
                    &args->notify
                ) == -1) {
                fprintf(
                    stderr,
                    "mnp: --notify-at must be between %d and %d\n",
                    NONE,
                    UNLOCKED
                );
                return -1;
            }
            break;

        case 'n':
            if (parse_integer(
                    optarg,
                    0,
                    INT_MAX,
                    &args->confirmation
                ) == -1) {
                fprintf(
                    stderr,
                    "mnp: --confirmation must be a non-negative integer\n"
                );
                return -1;
            }
            break;

        case '?':
        default:
            if (optopt != 0) {
                fprintf(
                    stderr,
                    "mnp: invalid option '-%c'\n",
                    optopt
                );
            } else {
                fprintf(
                    stderr,
                    "mnp: invalid monitor option\n"
                );
            }

            return -1;
        }
    }

    if (optind < argc) {
        args->txid_argument = argv[optind];
        ++optind;
    }

    if (optind < argc) {
        fprintf(
            stderr,
            "mnp: unexpected argument '%s'\n",
            argv[optind]
        );
        return -1;
    }

    return 0;
}

static char *read_monitor_txid(
    const struct monitor_args *args
)
{
    char *txid;

    if (args->txid_argument != NULL) {
        txid = strndup(args->txid_argument, MAX_TXID_SIZE);

        if (txid == NULL) {
            perror("mnp: strndup");
            return NULL;
        }

        return txid;
    }

    /*
     * Keep this temporary stdin implementation until txid_input.c
     * replaces it in the dedicated shared-input commit.
     */
    txid = malloc(MAX_TXID_SIZE + 1);

    if (txid == NULL) {
        perror("mnp: malloc");
        return NULL;
    }

    const size_t bytes = fread(txid, 1, MAX_TXID_SIZE, stdin);

    if (bytes != MAX_TXID_SIZE) {
        fprintf(
            stderr,
            "mnp: expected a %d-character TXID on stdin\n",
            MAX_TXID_SIZE
        );
        free(txid);
        return NULL;
    }

    txid[bytes] = '\0';

    return txid;
}

/*
 * Replace the beginning of the previous monitor_main() with this
 * argument handling. The remainder of the monitoring implementation
 * stays unchanged.
 */
int monitor_main(int argc, char **argv)
{
    struct monitor_args args;
    struct Config config = {0};
    struct rpc_wallet wallet = {0};

    char *ini = NULL;
    char *txid = NULL;

    cJSON *transfers = NULL;

    mode_t directory_mode;
    mode_t pipe_mode;

    int poll_interval;
    int remembered;
    int result = EXIT_FAILURE;

    if (parse_monitor_args(argc, argv, &args) == -1) {
        fprintf(
            stderr,
            "Usage: %s [TXID] "
            "[--notify-at 0|1|2|3] "
            "[--confirmation N]\n",
            argv[0]
        );
        return EXIT_FAILURE;
    }

    openlog("mnp:", LOG_PID, LOG_USER);

    signal(SIGHUP, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGQUIT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    ini = config_path();

    if (ini == NULL) {
        fprintf(
            stderr,
            "mnp: cannot determine config path\n"
        );
        goto done;
    }

    if (ini_parse(ini, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp: cannot load configuration '%s'\n",
            ini
        );
        goto done;
    }

    if (config.rpc_host == NULL ||
        config.rpc_port == NULL ||
        config.rpc_user == NULL ||
        config.rpc_password == NULL ||
        config.cfg_workdir == NULL ||
        config.cfg_mode == NULL ||
        config.cfg_pipe == NULL) {
        fprintf(
            stderr,
            "mnp: incomplete configuration\n"
        );
        goto done;
    }

    verbose =
        config.mnp_verbose != NULL
            ? atoi(config.mnp_verbose)
            : 0;

    txid = read_monitor_txid(&args);

    if (txid == NULL) {
        goto done;
    }

    if (val_hex_input(txid, MAX_TXID_SIZE) < 0) {
        fprintf(
            stderr,
            "mnp: invalid TXID\n"
        );
        goto done;
    }

    if (parse_permissions(
            config.cfg_mode,
            &directory_mode
        ) == -1 ||
        parse_permissions(
            config.cfg_pipe,
            &pipe_mode
        ) == -1) {
        fprintf(
            stderr,
            "mnp: invalid configured permissions\n"
        );
        goto done;
    }

    remembered = remember_txid(
        config.cfg_workdir,
        txid
    );

    if (remembered < 0) {
        goto done;
    }

    if (remembered == 1) {
        result = EXIT_SUCCESS;
        goto done;
    }

    if (init_wallet(
            &wallet,
            &config,
            txid
        ) == -1) {
        fprintf(
            stderr,
            "mnp: cannot initialize wallet RPC data\n"
        );
        goto done;
    }

    poll_interval =
        get_env_int(
            "MNP_POLL_INTERVAL",
            POLL_INTERVAL
        );

    if (args.notify == NONE) {
        result = EXIT_SUCCESS;
        goto done;
    }

    if (wait_for_transaction(
            &wallet,
            args.notify,
            args.confirmation,
            poll_interval,
            config.cfg_workdir,
            &transfers
        ) == -1) {
        goto done;
    }

    if (process_transfers(
            transfers,
            txid,
            config.cfg_workdir,
            pipe_mode
        ) == -1) {
        goto done;
    }

    result = EXIT_SUCCESS;

done:
    free_wallet(&wallet);
    free(txid);
    free(ini);
    free_config(&config);

    closelog();

    return result;
}
