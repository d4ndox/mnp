/* ============================================================
 * balance.c
 * ============================================================ */

#include "balance.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"

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
    } else if (MATCH("mnp", "account")) {
        config->mnp_account = strndup(value, MAX_DATA_SIZE);
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

static int init_wallet(
    struct rpc_wallet *wallet,
    const struct Config *config
)
{
    const char *account;

    if (wallet == NULL ||
        config == NULL ||
        config->rpc_host == NULL ||
        config->rpc_port == NULL ||
        config->rpc_user == NULL ||
        config->rpc_password == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    account =
        config->mnp_account != NULL
            ? config->mnp_account
            : "0";

    wallet->monero_rpc_method = GET_BALANCE;
    wallet->account = strndup(account, MAX_DATA_SIZE);
    wallet->host = strndup(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = strndup(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = strndup(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = strndup(config->rpc_password, MAX_DATA_SIZE);

    if (wallet->account == NULL ||
        wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL) {
        return -1;
    }

    return 0;
}

static int print_balance(const struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *balance;

    if (wallet == NULL || wallet->reply == NULL) {
        return -1;
    }

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return -1;
    }

    balance = cJSON_GetObjectItem(result, "balance");

    if (balance == NULL || !cJSON_IsNumber(balance)) {
        return -1;
    }

    /*
     * cJSON stores numbers as double. Using PrintUnformatted preserves
     * the JSON numeric representation without adding labels.
     */
    char *value = cJSON_PrintUnformatted(balance);

    if (value == NULL) {
        return -1;
    }

    fprintf(stdout, "%s\n", value);
    free(value);

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
    free((void *)config->mnp_account);
}

void balance_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s balance\n"
        "\n"
        "Print the wallet balance in atomic units.\n"
        "\n"
        "Output:\n"
        "  The raw wallet balance is written to stdout.\n",
        program
    );
}

int balance_main(int argc, char **argv)
{
    struct Config config = {0};
    struct rpc_wallet wallet = {0};

    char *ini = NULL;
    int result = EXIT_FAILURE;

    if (argc != 1) {
        fprintf(
            stderr,
            "mnp balance: unexpected argument '%s'\n"
            "Try '%s help' for usage.\n",
            argv[1],
            argv[0]
        );
        return EXIT_FAILURE;
    }

    ini = config_path();

    if (ini == NULL) {
        fprintf(
            stderr,
            "mnp balance: cannot determine config path\n"
        );
        goto done;
    }

    if (ini_parse(ini, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp balance: cannot load configuration '%s'\n",
            ini
        );
        goto done;
    }

    if (init_wallet(&wallet, &config) == -1) {
        fprintf(
            stderr,
            "mnp balance: incomplete RPC configuration\n"
        );
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp balance: could not connect to host %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    if (print_balance(&wallet) == -1) {
        fprintf(
            stderr,
            "mnp balance: invalid wallet RPC response\n"
        );
        goto done;
    }

    result = EXIT_SUCCESS;

done:
    free_wallet(&wallet);
    free_config(&config);
    free(ini);

    return result;
}

