/*
 * Copyright (c) 2026 d4ndo@proton.me
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"
#include "validate.h"

int verbose = 0;

static volatile sig_atomic_t running = 1;

struct monitor_args {
    int notify;
    int confirmation;
    const char *txid_argument;
};

static const struct option monitor_options[] = {
    {"notify-at", required_argument, NULL, 'o'},
    {"confirmation", required_argument, NULL, 'n'},
    {NULL, 0, NULL, 0},
};

static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *get_config_path(void);
static int parse_integer(const char *value, int minimum, int maximum, int *result);
static int parse_monitor_args(int argc, char **argv, struct monitor_args *args);
static char *read_txid(const struct monitor_args *args);
static int parse_permissions(const char *permissions, mode_t *mode);
static int get_env_int(const char *name, int fallback);
static void handle_signal(int signal_number);
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config, const char *txid);
static int remember_txid(const char *workdir, const char *txid);
static cJSON *get_transfers(struct rpc_wallet *wallet);
static int get_confirmations(const cJSON *transfer, int *confirmations);
static int is_unlocked(const cJSON *transfer, int *unlocked);
static int write_to_pipe(const char *path, const char *content);
static int send_rpc_connection_alert(const char *workdir, const char *txid);
static int poll_transaction(struct rpc_wallet *wallet, int notify, int confirmation, int poll_interval,
                            const char *workdir, cJSON **transfers);
static int ensure_transaction_directory(const char *workdir, const char *txid, mode_t mode, char **path);
static int ensure_fifo(const char *path, mode_t mode);
static int start_amount_writer(const char *fifo, const char *amount);
static int process_transfer(const cJSON *transfer, const char *txid, const char *workdir,
                            const char *tx_directory, mode_t pipe_mode);
static int process_transfers(cJSON *transfers, const char *txid, const char *workdir,
                             mode_t directory_mode, mode_t pipe_mode);
static void free_wallet(struct rpc_wallet *wallet);
static void free_config(struct Config *config);

/**
 * Monitors a Monero transaction until the configured notification state is reached.
 *
 * The transaction ID may be supplied as a positional argument or through standard input.
 * The --notify-at and --confirmation options control when the monitor emits the transaction
 * notification.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE if monitoring fails.
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
            "Usage: %s [TXID] [--notify-at 0|1|2|3] [--confirmation N]\n",
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

    ini = get_config_path();

    if (ini == NULL) {
        fprintf(stderr, "mnp: cannot determine config path\n");
        goto done;
    }

    if (ini_parse(ini, config_handler, &config) < 0) {
        fprintf(stderr, "mnp: cannot load configuration '%s'\n", ini);
        goto done;
    }

    if (config.rpc_host == NULL ||
        config.rpc_port == NULL ||
        config.rpc_user == NULL ||
        config.rpc_password == NULL ||
        config.cfg_workdir == NULL ||
        config.cfg_mode == NULL ||
        config.cfg_pipe == NULL) {
        fprintf(stderr, "mnp: incomplete configuration\n");
        goto done;
    }

    verbose = config.mnp_verbose != NULL
        ? atoi(config.mnp_verbose)
        : 0;

    txid = read_txid(&args);

    if (txid == NULL) {
        fprintf(stderr, "mnp: missing TXID\n");
        goto done;
    }

    if (val_hex_input(txid, MAX_TXID_SIZE) < 0) {
        fprintf(stderr, "mnp: invalid TXID\n");
        goto done;
    }

    if (parse_permissions(config.cfg_mode, &directory_mode) == -1) {
        fprintf(stderr, "mnp: invalid cfg.mode\n");
        goto done;
    }

    if (parse_permissions(config.cfg_pipe, &pipe_mode) == -1) {
        fprintf(stderr, "mnp: invalid cfg.pipe\n");
        goto done;
    }

    remembered = remember_txid(config.cfg_workdir, txid);

    if (remembered < 0) {
        goto done;
    }

    if (remembered == 1) {
        result = EXIT_SUCCESS;
        goto done;
    }

    if (init_wallet(&wallet, &config, txid) == -1) {
        fprintf(stderr, "mnp: cannot initialize wallet RPC request\n");
        goto done;
    }

    if (args.notify == NONE) {
        result = EXIT_SUCCESS;
        goto done;
    }

    poll_interval = get_env_int("MNP_POLL_INTERVAL", POLL_INTERVAL);
    running = 1;

    if (poll_transaction(
            &wallet,
            args.notify,
            args.confirmation,
            poll_interval,
            config.cfg_workdir,
            &transfers
        ) == -1) {
        goto done;
    }

    if (!running) {
        fprintf(stderr, "mnp: monitoring interrupted\n");
        goto done;
    }

    if (process_transfers(
            transfers,
            txid,
            config.cfg_workdir,
            directory_mode,
            pipe_mode
        ) == -1) {
        goto done;
    }

    result = EXIT_SUCCESS;

done:
    free_wallet(&wallet);
    free_config(&config);
    free(txid);
    free(ini);
    closelog();

    return result;
}

/**
 * Parses a configuration entry from the mnp configuration file.
 *
 * @param user A pointer to the Config structure receiving the parsed value.
 * @param section The configuration section name.
 * @param name The configuration option name.
 * @param value The configuration option value.
 * @return 1 if the configuration entry was handled, or 0 if it is unknown.
 */
static int config_handler(void *user, const char *section, const char *name, const char *value)
{
    struct Config *config = user;

#define MATCH(s, n) \
    (strcmp(section, (s)) == 0 && strcmp(name, (n)) == 0)

    if (MATCH("rpc", "user")) {
        config->rpc_user = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "password")) {
        config->rpc_password = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "host")) {
        config->rpc_host = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "port")) {
        config->rpc_port = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "verbose")) {
        config->mnp_verbose = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        config->mnp_account = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "workdir")) {
        config->cfg_workdir = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "mode")) {
        config->cfg_mode = strndup(value, MAX_DATA_SIZE);
    } else if (MATCH("cfg", "pipe")) {
        config->cfg_pipe = strndup(value, MAX_DATA_SIZE);
    } else {
        return 0;
    }

#undef MATCH

    return 1;
}

/**
 * Builds the path to the mnp configuration file in the user's home directory.
 *
 * @return A dynamically allocated string containing the configuration path, or NULL if the
 *         path cannot be created.
 */
static char *get_config_path(void)
{
    const char *home;
    char *path = NULL;

    home = getenv("HOME");

    if (home == NULL) {
        const struct passwd *entry = getpwuid(getuid());

        if (entry == NULL || entry->pw_dir == NULL) {
            return NULL;
        }

        home = entry->pw_dir;
    }

    if (asprintf(&path, "%s/%s", home, CONFIG_FILE) == -1) {
        return NULL;
    }

    return path;
}

/**
 * Parses an integer and validates that it is inside the specified range.
 *
 * @param value The string containing the integer value.
 * @param minimum The minimum accepted value.
 * @param maximum The maximum accepted value.
 * @param result A pointer receiving the parsed integer.
 * @return 0 on success, or -1 if the value is invalid.
 */
static int parse_integer(const char *value, int minimum, int maximum, int *result)
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

/**
 * Parses command-line options used by the transaction monitor.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @param args A pointer to the monitor_args structure receiving the parsed values.
 * @return 0 on success, or -1 if the arguments are invalid.
 */
static int parse_monitor_args(int argc, char **argv, struct monitor_args *args)
{
    int option;

    if (args == NULL) {
        return -1;
    }

    args->notify = CONFIRMED;
    args->confirmation = 0;
    args->txid_argument = NULL;

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
            if (parse_integer(optarg, NONE, UNLOCKED, &args->notify) == -1) {
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
            if (parse_integer(optarg, 0, INT_MAX, &args->confirmation) == -1) {
                fprintf(
                    stderr,
                    "mnp: --confirmation must be a non-negative integer\n"
                );
                return -1;
            }
            break;

        case '?':
        default:
            fprintf(
                stderr,
                "mnp: unknown monitor option '%s'\n",
                argv[optind - 1]
            );
            return -1;
        }
    }

    if (optind < argc) {
        args->txid_argument = argv[optind++];
    }

    if (optind < argc) {
        fprintf(stderr, "mnp: unexpected argument '%s'\n", argv[optind]);
        return -1;
    }

    return 0;
}

/**
 * Reads a transaction ID from a positional argument or standard input.
 *
 * @param args A pointer to the parsed monitor arguments.
 * @return A dynamically allocated transaction ID, or NULL if no valid input could be read.
 */
static char *read_txid(const struct monitor_args *args)
{
    char *txid;

    if (args->txid_argument != NULL) {
        return strndup(args->txid_argument, MAX_TXID_SIZE);
    }

    txid = malloc(MAX_TXID_SIZE + 1);

    if (txid == NULL) {
        return NULL;
    }

    if (fread(txid, 1, MAX_TXID_SIZE, stdin) != MAX_TXID_SIZE) {
        free(txid);
        return NULL;
    }

    txid[MAX_TXID_SIZE] = '\0';

    return txid;
}

/**
 * Converts a symbolic permission string into a mode_t value.
 *
 * @param permissions A nine-character permission string such as "rwx------".
 * @param mode A pointer receiving the converted permission mode.
 * @return 0 on success, or -1 if the permission string is invalid.
 */
static int parse_permissions(const char *permissions, mode_t *mode)
{
    mode_t result = 0;
    size_t i;

    if (permissions == NULL ||
        mode == NULL ||
        strlen(permissions) != 9) {
        return -1;
    }

    for (i = 0; i < 9; ++i) {
        char expected;

        switch (i % 3) {
        case 0:
            expected = 'r';
            break;
        case 1:
            expected = 'w';
            break;
        default:
            expected = 'x';
            break;
        }

        if (permissions[i] != expected && permissions[i] != '-') {
            return -1;
        }

        if (permissions[i] == expected) {
            result |= (mode_t)1 << (8 - i);
        }
    }

    *mode = result;

    return 0;
}

/**
 * Reads an integer environment variable.
 *
 * @param name The environment variable name.
 * @param fallback The value returned if the environment variable is missing or invalid.
 * @return The parsed environment value or the supplied fallback value.
 */
static int get_env_int(const char *name, int fallback)
{
    const char *value;
    char *end = NULL;
    long parsed;

    value = getenv(name);

    if (value == NULL || *value == '\0') {
        return fallback;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);

    if (errno == ERANGE ||
        end == value ||
        *end != '\0' ||
        parsed < 0 ||
        parsed > INT_MAX) {
        return fallback;
    }

    return (int)parsed;
}

/**
 * Handles termination signals received by the transaction monitor.
 *
 * @param signal_number The received signal number.
 */
static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

/**
 * Initializes a Monero wallet RPC request for transaction monitoring.
 *
 * @param wallet A pointer to the rpc_wallet structure to initialize.
 * @param config A pointer to the loaded mnp configuration.
 * @param txid The transaction ID to monitor.
 * @return 0 on success, or -1 if initialization fails.
 */
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config, const char *txid)
{
    const char *account;

    if (wallet == NULL ||
        config == NULL ||
        txid == NULL ||
        config->rpc_host == NULL ||
        config->rpc_port == NULL ||
        config->rpc_user == NULL ||
        config->rpc_password == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    account = config->mnp_account != NULL
        ? config->mnp_account
        : "0";

    wallet->monero_rpc_method = GET_TXID;
    wallet->account = strndup(account, MAX_DATA_SIZE);
    wallet->host = strndup(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = strndup(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = strndup(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = strndup(config->rpc_password, MAX_DATA_SIZE);
    wallet->txid = strndup(txid, MAX_TXID_SIZE);

    if (wallet->account == NULL ||
        wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL ||
        wallet->txid == NULL) {
        return -1;
    }

    return 0;
}

/**
 * Stores a transaction ID in the mnp transaction ID history file.
 *
 * @param workdir The configured mnp working directory.
 * @param txid The transaction ID to store.
 * @return 0 if the transaction ID was added, 1 if it already existed, or -1 if the file
 *         operation fails.
 */
static int remember_txid(const char *workdir, const char *txid)
{
    char *path = NULL;
    FILE *file = NULL;
    char line[MAX_TXID_SIZE + 2];
    int found = 0;
    int result = -1;

    if (workdir == NULL || txid == NULL) {
        return -1;
    }

    if (asprintf(&path, "%s/%s", workdir, TMP_TXID_FILE) == -1) {
        return -1;
    }

    file = fopen(path, "a+");

    if (file == NULL) {
        fprintf(
            stderr,
            "mnp: cannot open '%s': %s\n",
            path,
            strerror(errno)
        );
        goto done;
    }

    rewind(file);

    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, txid) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        if (fseek(file, 0, SEEK_END) != 0 ||
            fprintf(file, "%s\n", txid) < 0) {
            fprintf(
                stderr,
                "mnp: cannot update '%s': %s\n",
                path,
                strerror(errno)
            );
            goto done;
        }
    }

    result = found ? 1 : 0;

done:
    if (file != NULL) {
        fclose(file);
    }

    free(path);

    return result;
}

/**
 * Extracts the transfers array from a Monero wallet RPC response.
 *
 * @param wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return A pointer to the transfers JSON array, or NULL if the extraction fails.
 */
static cJSON *get_transfers(struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *transfers;

    if (wallet == NULL || wallet->reply == NULL) {
        return NULL;
    }

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return NULL;
    }

    transfers = cJSON_GetObjectItem(result, "transfers");

    if (transfers == NULL || !cJSON_IsArray(transfers)) {
        return NULL;
    }

    return transfers;
}

/**
 * Extracts the confirmation count from a transfer object.
 *
 * @param transfer A pointer to the transfer JSON object.
 * @param confirmations A pointer receiving the confirmation count.
 * @return 0 on success, or -1 if the confirmation count cannot be extracted.
 */
static int get_confirmations(const cJSON *transfer, int *confirmations)
{
    cJSON *item;

    if (transfer == NULL || confirmations == NULL) {
        return -1;
    }

    item = cJSON_GetObjectItem(transfer, "confirmations");

    if (item == NULL || !cJSON_IsNumber(item)) {
        return -1;
    }

    *confirmations = item->valueint;

    return 0;
}

/**
 * Determines whether a transaction transfer is unlocked.
 *
 * @param transfer A pointer to the transfer JSON object.
 * @param unlocked A pointer receiving 1 if unlocked or 0 if locked.
 * @return 0 on success, or -1 if the locked state cannot be extracted.
 */
static int is_unlocked(const cJSON *transfer, int *unlocked)
{
    cJSON *locked;

    if (transfer == NULL || unlocked == NULL) {
        return -1;
    }

    locked = cJSON_GetObjectItem(transfer, "locked");

    if (locked == NULL || !cJSON_IsBool(locked)) {
        return -1;
    }

    *unlocked = cJSON_IsFalse(locked);

    return 0;
}

/**
 * Writes a string to a named pipe from a child process.
 *
 * @param path The filesystem path of the named pipe.
 * @param content The string to write to the named pipe.
 * @return 0 if the writer process was created, or -1 if the operation fails.
 */
static int write_to_pipe(const char *path, const char *content)
{
    pid_t pid;

    if (path == NULL || content == NULL || *path == '\0') {
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        fprintf(
            stderr,
            "mnp: cannot fork for FIFO '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (pid == 0) {
        int fd = open(path, O_WRONLY | O_CLOEXEC);

        if (fd == -1) {
            _exit(EXIT_FAILURE);
        }

        if (dprintf(fd, "%s\n", content) == -1) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        close(fd);
        _exit(EXIT_SUCCESS);
    }

    return 0;
}

/**
 * Sends a transaction ID to the RPC connection alert pipe.
 *
 * @param workdir The configured mnp working directory.
 * @param txid The transaction ID associated with the failed RPC request.
 * @return 0 if the writer process was created, or -1 if the operation fails.
 */
static int send_rpc_connection_alert(const char *workdir, const char *txid)
{
    char *path = NULL;
    int result;

    if (asprintf(&path, "%s/%s", workdir, RPC_CONN_ALERT) == -1) {
        return -1;
    }

    result = write_to_pipe(path, txid);

    free(path);

    return result;
}

/**
 * Polls the Monero wallet until the requested notification condition is reached.
 *
 * @param wallet A pointer to the configured rpc_wallet structure.
 * @param notify The requested notification stage.
 * @param confirmation The required number of confirmations.
 * @param poll_interval The delay between RPC requests in seconds.
 * @param workdir The configured mnp working directory.
 * @param transfers A pointer receiving the transfers JSON array.
 * @return 0 when the notification condition is reached, or -1 on failure.
 */
static int poll_transaction(struct rpc_wallet *wallet, int notify, int confirmation, int poll_interval,
                            const char *workdir, cJSON **transfers)
{
    while (running) {
        cJSON *transfer;
        int ready = 0;

        if (wallet->reply != NULL) {
            cJSON_Delete(wallet->reply);
            wallet->reply = NULL;
        }

        if (rpc_call(wallet) < 0) {
            fprintf(
                stderr,
                "mnp: could not connect to host: %s:%s\n",
                wallet->host,
                wallet->port
            );

            send_rpc_connection_alert(workdir, wallet->txid);
            return -1;
        }

        *transfers = get_transfers(wallet);

        if (*transfers == NULL) {
            fprintf(stderr, "mnp: invalid wallet RPC response\n");
            return -1;
        }

        if (cJSON_GetArraySize(*transfers) == 0) {
            sleep((unsigned int)poll_interval);
            continue;
        }

        transfer = cJSON_GetArrayItem(*transfers, 0);

        if (transfer == NULL) {
            sleep((unsigned int)poll_interval);
            continue;
        }

        switch (notify) {
        case TXPOOL:
            ready = 1;
            break;

	case CONFIRMED: {
    	    int confirmations;

	    if (get_confirmations(transfer, &confirmations) == -1) {
                fprintf(stderr, "mnp: confirmations missing in RPC response\n");
                return -1;
            }

            ready = confirmations >= confirmation;
            break;
        }	    

        case UNLOCKED: {
            int unlocked;

            if (is_unlocked(transfer, &unlocked) == -1) {
                fprintf(
                    stderr,
                    "mnp: locked status missing in RPC response\n"
                );
                return -1;
            }

            ready = unlocked;
            break;
        }

        default:
            fprintf(stderr, "mnp: invalid --notify-at value\n");
            return -1;
        }

        if (ready) {
            return 0;
        }

        sleep((unsigned int)poll_interval);
    }

    return -1;
}

/**
 * Creates the transaction-specific working directory when necessary.
 *
 * @param workdir The configured mnp working directory.
 * @param txid The transaction ID used as the directory name.
 * @param mode The filesystem permissions for the directory.
 * @param path A pointer receiving the dynamically allocated directory path.
 * @return 0 on success, or -1 if the directory cannot be created or validated.
 */
static int ensure_transaction_directory(const char *workdir, const char *txid, mode_t mode, char **path)
{
    struct stat status;

    if (asprintf(path, "%s/%s/%s", workdir, TRANSACTION_DIR, txid) == -1) {
        return -1;
    }

    if (stat(*path, &status) == 0) {
        if (!S_ISDIR(status.st_mode)) {
            fprintf(
                stderr,
                "mnp: '%s' exists but is not a directory\n",
                *path
            );
            return -1;
        }

        return 0;
    }

    if (errno != ENOENT) {
        fprintf(
            stderr,
            "mnp: cannot inspect '%s': %s\n",
            *path,
            strerror(errno)
        );
        return -1;
    }

    if (mkdir(*path, mode) == -1) {
        fprintf(
            stderr,
            "mnp: cannot create '%s': %s\n",
            *path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

/**
 * Creates a named pipe when it does not already exist.
 *
 * @param path The filesystem path of the named pipe.
 * @param mode The filesystem permissions for the named pipe.
 * @return 0 on success, or -1 if the pipe cannot be created or validated.
 */
static int ensure_fifo(const char *path, mode_t mode)
{
    struct stat status;

    if (lstat(path, &status) == 0) {
        if (!S_ISFIFO(status.st_mode)) {
            fprintf(
                stderr,
                "mnp: '%s' exists but is not a FIFO\n",
                path
            );
            return -1;
        }

        return 0;
    }

    if (errno != ENOENT) {
        fprintf(
            stderr,
            "mnp: cannot inspect FIFO '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (mkfifo(path, mode) == -1) {
        fprintf(
            stderr,
            "mnp: cannot create FIFO '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

/**
 * Starts a child process that writes a payment amount to a named pipe.
 *
 * @param fifo The filesystem path of the named pipe.
 * @param amount The payment amount to write.
 * @return 0 if the writer process was created, or -1 if fork fails.
 */
static int start_amount_writer(const char *fifo, const char *amount)
{
    pid_t pid;

    pid = fork();

    if (pid < 0) {
        fprintf(stderr, "mnp: cannot fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        int fd = open(fifo, O_WRONLY | O_CLOEXEC);

        if (fd == -1) {
            _exit(EXIT_FAILURE);
        }

        if (dprintf(fd, "%s\n", amount) == -1) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        close(fd);
        unlink(fifo);

        _exit(EXIT_SUCCESS);
    }

    return 0;
}

/**
 * Processes a single transfer returned by the Monero wallet RPC.
 *
 * @param transfer A pointer to the transfer JSON object.
 * @param txid The transaction ID associated with the transfer.
 * @param workdir The configured mnp working directory.
 * @param tx_directory The transaction-specific working directory.
 * @param pipe_mode The filesystem permissions for generated named pipes.
 * @return 0 on success, or -1 if processing fails.
 */
static int process_transfer(const cJSON *transfer, const char *txid, const char *workdir,
                            const char *tx_directory, mode_t pipe_mode)
{
    cJSON *address_item;
    cJSON *payment_id_item;
    cJSON *amount_item;
    cJSON *double_spend_item;
    const char *address;
    const char *payment_id;
    const char *identity;
    char *amount = NULL;
    char *fifo = NULL;
    char *alert_pipe = NULL;
    char *alert_content = NULL;
    char *txid_pipe = NULL;
    char *txid_content = NULL;
    int result = -1;

    address_item = cJSON_GetObjectItem(transfer, "address");
    payment_id_item = cJSON_GetObjectItem(transfer, "payment_id");
    amount_item = cJSON_GetObjectItem(transfer, "amount");
    double_spend_item = cJSON_GetObjectItem(transfer, "double_spend_seen");

    if (address_item == NULL ||
        !cJSON_IsString(address_item) ||
        address_item->valuestring == NULL ||
        payment_id_item == NULL ||
        !cJSON_IsString(payment_id_item) ||
        payment_id_item->valuestring == NULL ||
        amount_item == NULL ||
        !cJSON_IsNumber(amount_item) ||
        double_spend_item == NULL ||
        !cJSON_IsBool(double_spend_item)) {
        fprintf(stderr, "mnp: invalid transfer in RPC response\n");
        goto done;
    }

    address = address_item->valuestring;
    payment_id = payment_id_item->valuestring;

    identity = strcmp(payment_id, PAYNULL) != 0
        ? payment_id
        : address;

    amount = cJSON_PrintUnformatted(amount_item);

    if (amount == NULL) {
        goto done;
    }

    if (asprintf(&fifo, "%s/%s", tx_directory, identity) == -1) {
        fifo = NULL;
        goto done;
    }

    if (cJSON_IsTrue(double_spend_item)) {
        if (asprintf(&alert_pipe, "%s/%s", workdir, DS_ALERT_PIPE) == -1) {
            alert_pipe = NULL;
            goto done;
        }

        if (asprintf(&alert_content, "%s %s", txid, identity) == -1) {
            alert_content = NULL;
            goto done;
        }

        write_to_pipe(alert_pipe, alert_content);
    }

    if (ensure_fifo(fifo, pipe_mode) == -1) {
        goto done;
    }

    if (start_amount_writer(fifo, amount) == -1) {
        goto done;
    }

    if (asprintf(&txid_pipe, "%s/%s", workdir, TXID_PIPE) == -1) {
        txid_pipe = NULL;
        goto done;
    }

    if (asprintf(&txid_content, "%s %s", txid, identity) == -1) {
        txid_content = NULL;
        goto done;
    }

    write_to_pipe(txid_pipe, txid_content);

    result = 0;

done:
    free(txid_content);
    free(txid_pipe);
    free(alert_content);
    free(alert_pipe);
    free(fifo);
    free(amount);

    return result;
}

/**
 * Processes all transfers associated with a monitored transaction.
 *
 * @param transfers A pointer to the transfers JSON array.
 * @param txid The transaction ID associated with the transfers.
 * @param workdir The configured mnp working directory.
 * @param directory_mode The filesystem permissions for transaction directories.
 * @param pipe_mode The filesystem permissions for generated named pipes.
 * @return 0 on success, or -1 if processing fails.
 */
static int process_transfers(cJSON *transfers, const char *txid, const char *workdir,
                             mode_t directory_mode, mode_t pipe_mode)
{
    char *tx_directory = NULL;
    cJSON *transfer;
    int result = -1;

    if (ensure_transaction_directory(
            workdir,
            txid,
            directory_mode,
            &tx_directory
        ) == -1) {
        goto done;
    }

    cJSON_ArrayForEach(transfer, transfers) {
        if (process_transfer(
                transfer,
                txid,
                workdir,
                tx_directory,
                pipe_mode
            ) == -1) {
            goto done;
        }
    }

    result = 0;

done:
    free(tx_directory);

    return result;
}

/**
 * Releases all dynamically allocated fields in an rpc_wallet structure.
 *
 * @param wallet A pointer to the rpc_wallet structure to clean up.
 */
static void free_wallet(struct rpc_wallet *wallet)
{
    if (wallet == NULL) {
        return;
    }

    free(wallet->params);
    free(wallet->account);
    free(wallet->host);
    free(wallet->port);
    free(wallet->user);
    free(wallet->pwd);
    free(wallet->balance);
    free(wallet->height);
    free(wallet->file);
    free(wallet->txid);
    free(wallet->payid);
    free(wallet->saddr);
    free(wallet->iaddr);
    free(wallet->amount);
    free(wallet->conf);
    free(wallet->locked);
    free(wallet->fifo);
    free(wallet->message);
    free(wallet->signature);
    free(wallet->proof);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }

    memset(wallet, 0, sizeof(*wallet));
}

/**
 * Releases all dynamically allocated fields in an mnp configuration structure.
 *
 * @param config A pointer to the Config structure to clean up.
 */
static void free_config(struct Config *config)
{
    if (config == NULL) {
        return;
    }

    free((void *)config->rpc_user);
    free((void *)config->rpc_password);
    free((void *)config->rpc_host);
    free((void *)config->rpc_port);
    free((void *)config->mnp_daemon);
    free((void *)config->mnp_verbose);
    free((void *)config->mnp_account);
    free((void *)config->cfg_workdir);
    free((void *)config->cfg_mode);
    free((void *)config->cfg_pipe);

    memset(config, 0, sizeof(*config));
}
