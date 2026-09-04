/* ============================================================
 * tx_proof.c
 * ============================================================ */

#include "tx_proof.h"

#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"
#include "validate.h"
#include "wallet.h"

struct tx_proof_args {
    const char *txid_argument;
    char *address;
    char *signature;
    char *message;
};

static const struct option tx_proof_options[] = {
    {"address", required_argument, NULL, 'a'},
    {"signature", required_argument, NULL, 's'},
    {"message", required_argument, NULL, 'm'},
    {NULL, 0, NULL, 0},
};

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

static int parse_arguments(
    int argc,
    char **argv,
    struct tx_proof_args *args
)
{
    int option;

    if (args == NULL) {
        return -1;
    }

    memset(args, 0, sizeof(*args));

    optind = 1;
    opterr = 0;

    while ((option = getopt_long(
                argc,
                argv,
                "",
                tx_proof_options,
                NULL
            )) != -1) {
        switch (option) {
        case 'a':
            free(args->address);
            args->address = strndup(optarg, MAX_DATA_SIZE);

            if (args->address == NULL) {
                return -1;
            }
            break;

        case 's':
            free(args->signature);
            args->signature = strndup(optarg, MAX_DATA_SIZE);

            if (args->signature == NULL) {
                return -1;
            }
            break;

        case 'm':
            free(args->message);
            args->message = strndup(optarg, MAX_DATA_SIZE);

            if (args->message == NULL) {
                return -1;
            }
            break;

        case '?':
        default:
            fprintf(
                stderr,
                "mnp tx-proof: invalid option\n"
            );
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
            "mnp tx-proof: unexpected argument '%s'\n",
            argv[optind]
        );
        return -1;
    }

    if (args->address == NULL ||
        args->address[0] == '\0') {
        fprintf(
            stderr,
            "mnp tx-proof: --address is required\n"
        );
        return -1;
    }

    if (args->signature == NULL ||
        args->signature[0] == '\0') {
        fprintf(
            stderr,
            "mnp tx-proof: --signature is required\n"
        );
        return -1;
    }

    return 0;
}

static char *read_txid(
    const struct tx_proof_args *args
)
{
    char *txid;

    if (args->txid_argument != NULL) {
        return strndup(
            args->txid_argument,
            MAX_TXID_SIZE
        );
    }

    txid = malloc(MAX_TXID_SIZE + 1);

    if (txid == NULL) {
        return NULL;
    }

    const size_t bytes =
        fread(txid, 1, MAX_TXID_SIZE, stdin);

    if (bytes != MAX_TXID_SIZE) {
        free(txid);
        return NULL;
    }

    txid[bytes] = '\0';

    return txid;
}

static int init_wallet(
    struct rpc_wallet *wallet,
    const struct Config *config,
    const char *txid,
    const struct tx_proof_args *args
)
{
    if (wallet == NULL ||
        config == NULL ||
        txid == NULL ||
        args == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    wallet->monero_rpc_method = CHECK_TX_PROOF;

    wallet->host = strndup(
        config->rpc_host,
        MAX_DATA_SIZE
    );
    wallet->port = strndup(
        config->rpc_port,
        MAX_DATA_SIZE
    );
    wallet->user = strndup(
        config->rpc_user,
        MAX_DATA_SIZE
    );
    wallet->pwd = strndup(
        config->rpc_password,
        MAX_DATA_SIZE
    );
    wallet->txid = strndup(
        txid,
        MAX_TXID_SIZE
    );
    wallet->address = strndup(
        args->address,
        MAX_DATA_SIZE
    );
    wallet->signature = strndup(
        args->signature,
        MAX_DATA_SIZE
    );

    if (args->message != NULL) {
        wallet->message = strndup(
            args->message,
            MAX_DATA_SIZE
        );
    }

    if (wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL ||
        wallet->txid == NULL ||
        wallet->address == NULL ||
        wallet->signature == NULL ||
        (args->message != NULL &&
         wallet->message == NULL)) {
        return -1;
    }

    return 0;
}

static int proof_is_good(
    const struct rpc_wallet *wallet
)
{
    cJSON *result;
    cJSON *good;

    if (wallet == NULL ||
        wallet->reply == NULL) {
        return -1;
    }

    result = cJSON_GetObjectItem(
        wallet->reply,
        "result"
    );

    if (result == NULL) {
        return -1;
    }

    good = cJSON_GetObjectItem(
        result,
        "good"
    );

    if (good == NULL ||
        !cJSON_IsBool(good)) {
        return -1;
    }

    return cJSON_IsTrue(good) ? 1 : 0;
}

static void free_wallet(
    struct rpc_wallet *wallet
)
{
    if (wallet == NULL) {
        return;
    }

    free(wallet->host);
    free(wallet->port);
    free(wallet->user);
    free(wallet->pwd);
    free(wallet->txid);
    free(wallet->address);
    free(wallet->signature);
    free(wallet->message);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

static void free_config(
    struct Config *config
)
{
    if (config == NULL) {
        return;
    }

    free((void *)config->rpc_user);
    free((void *)config->rpc_password);
    free((void *)config->rpc_host);
    free((void *)config->rpc_port);
}

static void free_args(
    struct tx_proof_args *args
)
{
    if (args == NULL) {
        return;
    }

    free(args->address);
    free(args->signature);
    free(args->message);
}

void tx_proof_help(
    FILE *stream,
    const char *program
)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s tx-proof TXID --address ADDRESS "
        "--signature SIGNATURE [--message MESSAGE]\n"
        "  echo TXID | %s tx-proof --address ADDRESS "
        "--signature SIGNATURE [--message MESSAGE]\n"
        "\n"
        "Verify a Monero transaction proof.\n"
        "\n"
        "Arguments:\n"
        "  TXID                  Transaction ID. May be read from stdin.\n"
        "\n"
        "Options:\n"
        "  --address ADDRESS     Destination address. Required.\n"
        "  --signature SIGNATURE Transaction-proof signature. Required.\n"
        "  --message MESSAGE     Optional proof message.\n"
        "\n"
        "Output:\n"
        "  true                  Proof is valid.\n"
        "  false                 Proof is invalid.\n"
        "\n"
        "Examples:\n"
        "  %s tx-proof TXID --address ADDRESS "
        "--signature SIGNATURE\n"
        "  echo TXID | %s tx-proof --address ADDRESS "
        "--signature SIGNATURE\n",
        program,
        program,
        program,
        program
    );
}

int tx_proof_main(
    int argc,
    char **argv
)
{
    struct tx_proof_args args;
    struct Config config = {0};
    struct rpc_wallet wallet = {0};

    char *ini = NULL;
    char *txid = NULL;

    int good;
    int result = EXIT_FAILURE;

    if (parse_arguments(
            argc,
            argv,
            &args
        ) == -1) {
        fprintf(
            stderr,
            "Try '%s help' for usage.\n",
            argv[0]
        );
        return EXIT_FAILURE;
    }

    txid = read_txid(&args);

    if (txid == NULL) {
        fprintf(
            stderr,
            "mnp tx-proof: missing or invalid TXID input\n"
        );
        goto done;
    }

    if (val_hex_input(
            txid,
            MAX_TXID_SIZE
        ) < 0) {
        fprintf(
            stderr,
            "mnp tx-proof: invalid TXID\n"
        );
        goto done;
    }

    ini = config_path();

    if (ini == NULL) {
        fprintf(
            stderr,
            "mnp tx-proof: cannot determine config path\n"
        );
        goto done;
    }

    if (ini_parse(
            ini,
            config_handler,
            &config
        ) < 0) {
        fprintf(
            stderr,
            "mnp tx-proof: cannot load configuration '%s'\n",
            ini
        );
        goto done;
    }

    if (config.rpc_host == NULL ||
        config.rpc_port == NULL ||
        config.rpc_user == NULL ||
        config.rpc_password == NULL) {
        fprintf(
            stderr,
            "mnp tx-proof: incomplete RPC configuration\n"
        );
        goto done;
    }

    if (init_wallet(
            &wallet,
            &config,
            txid,
            &args
        ) == -1) {
        fprintf(
            stderr,
            "mnp tx-proof: cannot initialize RPC request\n"
        );
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp tx-proof: could not connect to host: %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    good = proof_is_good(&wallet);

    if (good < 0) {
        fprintf(
            stderr,
            "mnp tx-proof: invalid wallet RPC response\n"
        );
        goto done;
    }

    if (good == 1) {
        fprintf(stdout, "true\n");
        result = EXIT_SUCCESS;
    } else {
        fprintf(stdout, "false\n");
        result = EXIT_FAILURE;
    }

done:
    free_wallet(&wallet);
    free(txid);
    free(ini);
    free_config(&config);
    free_args(&args);

    return result;
}
