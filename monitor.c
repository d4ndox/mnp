/* ============================================================
 * monitor.c
 * ============================================================ */

#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#include "delquotes.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"
#include "validate.h"
#include "wallet.h"

static volatile sig_atomic_t running = 1;

static int config_handler(
    void *user,
    const char *section,
    const char *name,
    const char *value
);

static char *config_path(void);
static char *read_txid(int argc, char **argv);
static int parse_permissions(const char *permissions, mode_t *mode);
static int get_env_int(const char *name, int fallback);
static void handle_signal(int signal_number);

static int init_wallet(
    struct rpc_wallet *wallet,
    const struct Config *config,
    const char *txid
);

static int remember_txid(const char *workdir, const char *txid);

static cJSON *get_transfers(struct rpc_wallet *wallet);
static char *get_confirm(const cJSON *transfer);
static char *get_locked(const cJSON *transfer);

static int write_to_pipe(const char *path, const char *content);

static int wait_for_transaction(
    struct rpc_wallet *wallet,
    int notify,
    int confirmation,
    int poll_interval,
    const char *workdir,
    cJSON **transfers
);

static int process_transfers(
    cJSON *transfers,
    const char *txid,
    const char *workdir,
    mode_t pipe_mode
);

static void free_wallet(struct rpc_wallet *wallet);
static void free_config(struct Config *config);

static int config_handler(
    void *user,
    const char *section,
    const char *name,
    const char *value
)
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

static char *config_path(void)
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

static char *read_txid(int argc, char **argv)
{
    char *txid = NULL;

    if (argc >= 2) {
        txid = strndup(argv[1], MAX_TXID_SIZE);
    } else {
        txid = malloc(MAX_TXID_SIZE + 1);

        if (txid == NULL) {
            return NULL;
        }

        const size_t bytes = fread(txid, 1, MAX_TXID_SIZE, stdin);

        if (bytes != MAX_TXID_SIZE) {
            free(txid);
            return NULL;
        }

        txid[bytes] = '\0';
    }

    if (txid == NULL) {
        return NULL;
    }

    if (val_hex_input(txid, MAX_TXID_SIZE) < 0) {
        free(txid);
        return NULL;
    }

    return txid;
}

static int parse_permissions(const char *permissions, mode_t *mode)
{
    mode_t result = 0;

    if (permissions == NULL ||
        mode == NULL ||
        strlen(permissions) != 9) {
        return -1;
    }

    for (size_t i = 0; i < 9; ++i) {
        const char expected =
            (i % 3 == 0) ? 'r' :
            (i % 3 == 1) ? 'w' : 'x';

        if (permissions[i] != expected &&
            permissions[i] != '-') {
            return -1;
        }

        if (permissions[i] == expected) {
            result |= (mode_t)1 << (8 - i);
        }
    }

    *mode = result;

    return 0;
}

static int get_env_int(const char *name, int fallback)
{
    const char *value = getenv(name);

    if (value == NULL || *value == '\0') {
        return fallback;
    }

    char *end = NULL;
    const long parsed = strtol(value, &end, 10);

    if (end == NULL || *end != '\0') {
        return fallback;
    }

    return (int)parsed;
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static int init_wallet(
    struct rpc_wallet *wallet,
    const struct Config *config,
    const char *txid
)
{
    if (wallet == NULL ||
        config == NULL ||
        txid == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    wallet->monero_rpc_method = GET_TXID;

    wallet->account = strndup(
        config->mnp_account != NULL
            ? config->mnp_account
            : "0",
        MAX_DATA_SIZE
    );

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

static int remember_txid(const char *workdir, const char *txid)
{
    char *path = NULL;
    FILE *file = NULL;
    char line[MAX_TXID_SIZE + 2];
    int found = 0;
    int result = -1;

    if (asprintf(&path, "%s/%s", workdir, TMP_TXID_FILE) == -1) {
        return -1;
    }

    file = fopen(path, "a+");

    if (file == NULL) {
        fprintf(
            stderr,
            "mnp: cannot open txid file '%s': %s\n",
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
        if (fprintf(file, "%s\n", txid) < 0) {
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

static cJSON *get_transfers(struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *transfers;

    assert(wallet != NULL);

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL) {
        return NULL;
    }

    transfers = cJSON_GetObjectItem(result, "transfers");

    if (transfers == NULL) {
        return NULL;
    }

    return transfers;
}

static char *get_confirm(const cJSON *transfer)
{
    cJSON *confirmations;

    assert(transfer != NULL);

    confirmations = cJSON_GetObjectItem(transfer, "confirmations");

    if (confirmations == NULL) {
        return NULL;
    }

    return cJSON_Print(confirmations);
}

static char *get_locked(const cJSON *transfer)
{
    cJSON *locked;

    assert(transfer != NULL);

    locked = cJSON_GetObjectItem(transfer, "locked");

    if (locked == NULL) {
        return NULL;
    }

    return cJSON_Print(locked);
}

static int write_to_pipe(const char *path, const char *content)
{
    pid_t pid;

    if (path == NULL ||
        content == NULL ||
        *path == '\0') {
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        fprintf(
            stderr,
            "mnp: cannot fork for pipe '%s': %s\n",
            path,
            strerror(errno)
        );
        return -1;
    }

    if (pid == 0) {
        const int fd = open(path, O_WRONLY | O_CLOEXEC);

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

static int wait_for_transaction(
    struct rpc_wallet *wallet,
    int notify,
    int confirmation,
    int poll_interval,
    const char *workdir,
    cJSON **transfers
)
{
    int jail = 1;

    running = 1;

    while (running && jail) {
        if (rpc_call(wallet) < 0) {
            char *pipe = NULL;

            fprintf(
                stderr,
                "mnp: could not connect to host: %s:%s\n",
                wallet->host,
                wallet->port
            );

            if (asprintf(
                    &pipe,
                    "%s/%s",
                    workdir,
                    RPC_CONN_ALERT
                ) != -1) {
                write_to_pipe(pipe, wallet->txid);
                free(pipe);
            }

            return -1;
        }

        *transfers = get_transfers(wallet);

        if (*transfers == NULL) {
            fprintf(stderr, "mnp: invalid wallet RPC response\n");
            return -1;
        }

        const cJSON *transfer =
            cJSON_GetArrayItem(*transfers, 0);

        if (transfer == NULL) {
            sleep(poll_interval);
            continue;
        }

        switch (notify) {
        case TXPOOL:
            jail = 0;
            break;

        case CONFIRMED: {
            char *confirmations = get_confirm(transfer);

            if (confirmations == NULL) {
                return -1;
            }

            if (atoi(confirmations) >= confirmation) {
                jail = 0;
            }

            free(confirmations);
            break;
        }

        case UNLOCKED: {
            char *locked = get_locked(transfer);

            if (locked == NULL) {
                return -1;
            }

            if (strcmp(locked, "false") == 0) {
                jail = 0;
            }

            free(locked);
            break;
        }

        default:
            fprintf(stderr, "mnp: invalid notify level\n");
            return -1;
        }

        if (jail) {
            sleep(poll_interval);
        }
    }

    return running ? 0 : -1;
}

static int process_transfers(
    cJSON *transfers,
    const char *txid,
    const char *workdir,
    mode_t pipe_mode
)
{
    char *tx_directory = NULL;
    struct stat status;
    cJSON *transfer;

    if (asprintf(
            &tx_directory,
            "%s/%s/%s",
            workdir,
            TRANSACTION_DIR,
            txid
        ) == -1) {
        return -1;
    }

    if (stat(tx_directory, &status) == -1) {
        if (errno != ENOENT ||
            mkdir(tx_directory, pipe_mode) == -1) {
            fprintf(
                stderr,
                "mnp: cannot create transaction directory '%s': %s\n",
                tx_directory,
                strerror(errno)
            );
            free(tx_directory);
            return -1;
        }
    }

    cJSON_ArrayForEach(transfer, transfers) {
        cJSON *address_json;
        cJSON *payment_id_json;
        cJSON *amount_json;
        cJSON *double_spend_json;

        char *address = NULL;
        char *payment_id = NULL;
        char *amount = NULL;
        char *double_spend = NULL;
        char *identity = NULL;
        char *fifo = NULL;

        address_json = cJSON_GetObjectItem(transfer, "address");
        payment_id_json = cJSON_GetObjectItem(transfer, "payment_id");
        amount_json = cJSON_GetObjectItem(transfer, "amount");
        double_spend_json =
            cJSON_GetObjectItem(transfer, "double_spend_seen");

        if (address_json == NULL ||
            payment_id_json == NULL ||
            amount_json == NULL ||
            double_spend_json == NULL) {
            free(tx_directory);
            return -1;
        }

        address = delQuotes(cJSON_Print(address_json));
        payment_id = delQuotes(cJSON_Print(payment_id_json));
        amount = cJSON_Print(amount_json);
        double_spend = cJSON_Print(double_spend_json);

        if (address == NULL ||
            payment_id == NULL ||
            amount == NULL ||
            double_spend == NULL) {
            free(address);
            free(payment_id);
            free(amount);
            free(double_spend);
            free(tx_directory);
            return -1;
        }

        identity =
            strcmp(payment_id, PAYNULL) != 0
                ? payment_id
                : address;

        if (asprintf(
                &fifo,
                "%s/%s",
                tx_directory,
                identity
            ) == -1) {
            free(address);
            free(payment_id);
            free(amount);
            free(double_spend);
            free(tx_directory);
            return -1;
        }

        if (strcmp(double_spend, "true") == 0) {
            char *alert_pipe = NULL;
            char *content = NULL;

            if (asprintf(
                    &alert_pipe,
                    "%s/%s",
                    workdir,
                    DS_ALERT_PIPE
                ) != -1 &&
                asprintf(
                    &content,
                    "%s %s",
                    txid,
                    identity
                ) != -1) {
                write_to_pipe(alert_pipe, content);
            }

            free(alert_pipe);
            free(content);
        }

        if (stat(fifo, &status) == -1) {
            if (errno != ENOENT ||
                mkfifo(fifo, pipe_mode) == -1) {
                fprintf(
                    stderr,
                    "mnp: cannot create FIFO '%s': %s\n",
                    fifo,
                    strerror(errno)
                );

                free(fifo);
                free(address);
                free(payment_id);
                free(amount);
                free(double_spend);
                free(tx_directory);
                return -1;
            }
        }

        pid_t pid = fork();

        if (pid < 0) {
            free(fifo);
            free(address);
            free(payment_id);
            free(amount);
            free(double_spend);
            free(tx_directory);
            return -1;
        }

        if (pid == 0) {
            const int fd = open(fifo, O_WRONLY | O_CLOEXEC);

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

        char *txid_pipe = NULL;
        char *txid_content = NULL;

        if (asprintf(
                &txid_pipe,
                "%s/%s",
                workdir,
                TXID_PIPE
            ) != -1 &&
            asprintf(
                &txid_content,
                "%s %s",
                txid,
                identity
            ) != -1) {
            write_to_pipe(txid_pipe, txid_content);
        }

        free(txid_content);
        free(txid_pipe);
        free(fifo);
        free(address);
        free(payment_id);
        free(amount);
        free(double_spend);
    }

    free(tx_directory);

    return 0;
}

static void free_wallet(struct rpc_wallet *wallet)
{
    if (wallet == NULL) {
        return;
    }

    free(wallet->account);
    free(wallet->host);
    free(wallet->port);
    free(wallet->user);
    free(wallet->pwd);
    free(wallet->txid);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

static void free_config(struct Config *config)
{
    if (config == NULL) {
        return;
    }

    free((void *)config->rpc_user);
    free((void *)config->rpc_password);
    free((void *)config->rpc_host);
    free((void *)config->rpc_port);
    free((void *)config->mnp_verbose);
    free((void *)config->mnp_account);
    free((void *)config->cfg_workdir);
    free((void *)config->cfg_mode);
    free((void *)config->cfg_pipe);
}

int monitor_main(int argc, char **argv)
{
    struct Config config = {0};
    struct rpc_wallet wallet = {0};

    char *ini = NULL;
    char *txid = NULL;

    cJSON *transfers = NULL;

    mode_t directory_mode;
    mode_t pipe_mode;

    int notify = CONFIRMED;
    int confirmation = 0;
    int poll_interval;
    int remembered;
    int result = EXIT_FAILURE;

    if (argc > 2) {
        fprintf(
            stderr,
            "mnp: unexpected argument '%s'\n"
            "Try '%s help' for usage.\n",
            argv[2],
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
        fprintf(stderr, "mnp: cannot determine config path\n");
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
        fprintf(stderr, "mnp: incomplete configuration\n");
        goto done;
    }

    verbose =
        config.mnp_verbose != NULL
            ? atoi(config.mnp_verbose)
            : 0;

    txid = read_txid(argc, argv);

    if (txid == NULL) {
        fprintf(stderr, "mnp: no input data or invalid txid\n");
        goto done;
    }

    if (parse_permissions(config.cfg_mode, &directory_mode) == -1 ||
        parse_permissions(config.cfg_pipe, &pipe_mode) == -1) {
        fprintf(stderr, "mnp: invalid configured permissions\n");
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
        fprintf(stderr, "mnp: cannot initialize wallet RPC data\n");
        goto done;
    }

    poll_interval =
        get_env_int("MNP_POLL_INTERVAL", POLL_INTERVAL);

    if (notify == NONE) {
        result = EXIT_SUCCESS;
        goto done;
    }

    if (wait_for_transaction(
            &wallet,
            notify,
            confirmation,
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
