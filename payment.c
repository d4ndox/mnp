/* ============================================================
 * payment.c
 * ============================================================ */

#include "payment.h"

#include <errno.h>
#include <limits.h>
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

enum payment_action {
    PAYMENT_ACTION_INTEGRATED,
    PAYMENT_ACTION_NEW,
    PAYMENT_ACTION_LIST,
    PAYMENT_ACTION_SUBADDR
};

struct payment_args {
    enum payment_action action;
    const char *payment_id_argument;
    char *amount;
    int subaddress_index;
};

static char *duplicate_string(const char *value, size_t maximum)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strnlen(value, maximum + 1);

    if (length > maximum) {
        return NULL;
    }

    copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length);
    copy[length] = '\0';

    return copy;
}

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
        config->rpc_user = duplicate_string(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "password")) {
        config->rpc_password = duplicate_string(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "host")) {
        config->rpc_host = duplicate_string(value, MAX_DATA_SIZE);
    } else if (MATCH("rpc", "port")) {
        config->rpc_port = duplicate_string(value, MAX_DATA_SIZE);
    } else if (MATCH("mnp", "account")) {
        config->mnp_account = duplicate_string(value, MAX_DATA_SIZE);
    } else {
        return 0;
    }

#undef MATCH

    return 1;
}

static char *get_config_path(void)
{
    const char *home;
    size_t length;
    char *path;

    home = getenv("HOME");

    if (home == NULL) {
        const struct passwd *entry = getpwuid(getuid());

        if (entry == NULL || entry->pw_dir == NULL) {
            return NULL;
        }

        home = entry->pw_dir;
    }

    length = strlen(home) + 1 + strlen(CONFIG_FILE) + 1;

    path = malloc(length);

    if (path == NULL) {
        return NULL;
    }

    if (snprintf(
            path,
            length,
            "%s/%s",
            home,
            CONFIG_FILE
        ) < 0) {
        free(path);
        return NULL;
    }

    return path;
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

static int parse_nonnegative_int(
    const char *value,
    int *result
)
{
    char *end = NULL;
    long parsed;

    if (value == NULL ||
        *value == '\0' ||
        result == NULL) {
        return -1;
    }

    errno = 0;
    parsed = strtol(value, &end, 10);

    if (errno == ERANGE ||
        end == value ||
        *end != '\0' ||
        parsed < 0 ||
        parsed > INT_MAX) {
        return -1;
    }

    *result = (int)parsed;

    return 0;
}

static int parse_arguments(
    int argc,
    char **argv,
    struct payment_args *args
)
{
    const char *positionals[2] = {NULL, NULL};
    size_t positional_count = 0;
    int i;

    if (args == NULL) {
        return -1;
    }

    memset(args, 0, sizeof(*args));

    args->action = PAYMENT_ACTION_INTEGRATED;
    args->subaddress_index = -1;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--amount") == 0) {
            if (args->amount != NULL) {
                fprintf(
                    stderr,
                    "mnp payment: --amount specified more than once\n"
                );
                return -1;
            }

            if (++i >= argc) {
                fprintf(
                    stderr,
                    "mnp payment: --amount requires a value\n"
                );
                return -1;
            }

            args->amount = duplicate_string(
                argv[i],
                MAX_DATA_SIZE
            );

            if (args->amount == NULL) {
                fprintf(
                    stderr,
                    "mnp payment: invalid amount\n"
                );
                return -1;
            }

            if (val_amount(args->amount) < 0) {
                fprintf(
                    stderr,
                    "mnp payment: invalid amount '%s'\n",
                    args->amount
                );
                return -1;
            }

            continue;
        }

        if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(
                stderr,
                "mnp payment: unknown option '%s'\n",
                argv[i]
            );
            return -1;
        }

        if (positional_count >= 2) {
            fprintf(
                stderr,
                "mnp payment: unexpected argument '%s'\n",
                argv[i]
            );
            return -1;
        }

        positionals[positional_count++] = argv[i];
    }

    if (positional_count == 0) {
        args->action = PAYMENT_ACTION_INTEGRATED;
        return 0;
    }

    if (strcmp(positionals[0], "new") == 0) {
        if (positional_count != 1) {
            fprintf(
                stderr,
                "mnp payment: unexpected argument '%s'\n",
                positionals[1]
            );
            return -1;
        }

        args->action = PAYMENT_ACTION_NEW;
        return 0;
    }

    if (strcmp(positionals[0], "list") == 0) {
        if (positional_count != 1) {
            fprintf(
                stderr,
                "mnp payment: unexpected argument '%s'\n",
                positionals[1]
            );
            return -1;
        }

        if (args->amount != NULL) {
            fprintf(
                stderr,
                "mnp payment: --amount cannot be used with 'list'\n"
            );
            return -1;
        }

        args->action = PAYMENT_ACTION_LIST;
        return 0;
    }

    if (strcmp(positionals[0], "subaddr") == 0) {
        if (positional_count != 2) {
            fprintf(
                stderr,
                "mnp payment: 'subaddr' requires an index\n"
            );
            return -1;
        }

        if (parse_nonnegative_int(
                positionals[1],
                &args->subaddress_index
            ) == -1) {
            fprintf(
                stderr,
                "mnp payment: invalid subaddress index '%s'\n",
                positionals[1]
            );
            return -1;
        }

        args->action = PAYMENT_ACTION_SUBADDR;
        return 0;
    }

    if (positional_count != 1) {
        fprintf(
            stderr,
            "mnp payment: unexpected argument '%s'\n",
            positionals[1]
        );
        return -1;
    }

    args->action = PAYMENT_ACTION_INTEGRATED;
    args->payment_id_argument = positionals[0];

    return 0;
}

static char *read_payment_id_from_stdin(void)
{
    char buffer[MAX_PAYID_SIZE + 3];
    size_t length;

    if (isatty(STDIN_FILENO)) {
        return NULL;
    }

    if (fgets(
            buffer,
            sizeof(buffer),
            stdin
        ) == NULL) {
        return NULL;
    }

    length = strcspn(buffer, "\r\n");

    if (buffer[length] == '\0' &&
        length > MAX_PAYID_SIZE) {
        return NULL;
    }

    buffer[length] = '\0';

    if (length != MAX_PAYID_SIZE) {
        return NULL;
    }

    return duplicate_string(
        buffer,
        MAX_PAYID_SIZE
    );
}

static char *get_payment_id(
    const struct payment_args *args
)
{
    char *payment_id;

    if (args->payment_id_argument != NULL) {
        payment_id = duplicate_string(
            args->payment_id_argument,
            MAX_PAYID_SIZE
        );
    } else {
        payment_id = read_payment_id_from_stdin();
    }

    if (payment_id == NULL) {
        return NULL;
    }

    if (val_hex_input(
            payment_id,
            MAX_PAYID_SIZE
        ) < 0) {
        free(payment_id);
        return NULL;
    }

    return payment_id;
}

static int init_wallet(
    struct rpc_wallet *wallet,
    enum monero_rpc_method method,
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

    wallet->monero_rpc_method = method;
    wallet->account = duplicate_string(
        account,
        MAX_DATA_SIZE
    );
    wallet->host = duplicate_string(
        config->rpc_host,
        MAX_DATA_SIZE
    );
    wallet->port = duplicate_string(
        config->rpc_port,
        MAX_DATA_SIZE
    );
    wallet->user = duplicate_string(
        config->rpc_user,
        MAX_DATA_SIZE
    );
    wallet->pwd = duplicate_string(
        config->rpc_password,
        MAX_DATA_SIZE
    );

    if (wallet->account == NULL ||
        wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL) {
        return -1;
    }

    return 0;
}

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
    free(wallet->payid);
    free(wallet->saddr);
    free(wallet->iaddr);
    free(wallet->amount);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }

    memset(wallet, 0, sizeof(*wallet));
}

static int call_wallet(
    struct rpc_wallet *wallet,
    const char *command
)
{
    if (rpc_call(wallet) < 0) {
        fprintf(
            stderr,
            "mnp payment %s: could not connect to host %s:%s\n",
            command,
            wallet->host,
            wallet->port
        );
        return -1;
    }

    return 0;
}

static cJSON *get_result(
    const struct rpc_wallet *wallet
)
{
    cJSON *result;

    if (wallet == NULL ||
        wallet->reply == NULL) {
        return NULL;
    }

    result = cJSON_GetObjectItem(
        wallet->reply,
        "result"
    );

    if (result == NULL ||
        !cJSON_IsObject(result)) {
        return NULL;
    }

    return result;
}

static char *get_json_string(
    cJSON *object,
    const char *name
)
{
    cJSON *item;

    if (object == NULL || name == NULL) {
        return NULL;
    }

    item = cJSON_GetObjectItem(object, name);

    if (item == NULL ||
        !cJSON_IsString(item) ||
        item->valuestring == NULL) {
        return NULL;
    }

    return duplicate_string(
        item->valuestring,
        MAX_DATA_SIZE
    );
}

static int print_uri(
    const struct Config *config,
    const char *address,
    const char *amount
)
{
    struct rpc_wallet wallet;
    cJSON *result;
    char *uri = NULL;
    int status = -1;

    if (init_wallet(
            &wallet,
            MK_URI,
            config
        ) == -1) {
        fprintf(
            stderr,
            "mnp payment: cannot initialize make_uri request\n"
        );
        return -1;
    }

    wallet.saddr = duplicate_string(
        address,
        MAX_IADDR_SIZE
    );
    wallet.amount = duplicate_string(
        amount,
        MAX_DATA_SIZE
    );

    if (wallet.saddr == NULL ||
        wallet.amount == NULL) {
        goto done;
    }

    if (call_wallet(
            &wallet,
            "make-uri"
        ) == -1) {
        goto done;
    }

    result = get_result(&wallet);
    uri = get_json_string(result, "uri");

    if (uri == NULL) {
        fprintf(
            stderr,
            "mnp payment: invalid make_uri RPC response\n"
        );
        goto done;
    }

    fprintf(stdout, "%s\n", uri);
    status = 0;

done:
    free(uri);
    free_wallet(&wallet);

    return status;
}

static int run_new(
    const struct Config *config,
    const char *amount
)
{
    struct rpc_wallet wallet;
    cJSON *result;
    char *address = NULL;
    int status = EXIT_FAILURE;

    if (init_wallet(
            &wallet,
            NEW_SUBADDR,
            config
        ) == -1) {
        fprintf(
            stderr,
            "mnp payment new: cannot initialize RPC request\n"
        );
        return EXIT_FAILURE;
    }

    if (call_wallet(
            &wallet,
            "new"
        ) == -1) {
        goto done;
    }

    result = get_result(&wallet);
    address = get_json_string(result, "address");

    if (address == NULL) {
        fprintf(
            stderr,
            "mnp payment new: invalid wallet RPC response\n"
        );
        goto done;
    }

    if (amount == NULL) {
        fprintf(stdout, "%s\n", address);
        status = EXIT_SUCCESS;
        goto done;
    }

    if (print_uri(
            config,
            address,
            amount
        ) == -1) {
        goto done;
    }

    status = EXIT_SUCCESS;

done:
    free(address);
    free_wallet(&wallet);

    return status;
}

static int run_list(
    const struct Config *config
)
{
    struct rpc_wallet wallet;
    cJSON *result;
    cJSON *addresses;
    int size;
    int i;
    int status = EXIT_FAILURE;

    if (init_wallet(
            &wallet,
            GET_LIST,
            config
        ) == -1) {
        fprintf(
            stderr,
            "mnp payment list: cannot initialize RPC request\n"
        );
        return EXIT_FAILURE;
    }

    if (call_wallet(
            &wallet,
            "list"
        ) == -1) {
        goto done;
    }

    result = get_result(&wallet);

    if (result == NULL) {
        fprintf(
            stderr,
            "mnp payment list: invalid wallet RPC response\n"
        );
        goto done;
    }

    addresses = cJSON_GetObjectItem(
        result,
        "addresses"
    );

    if (addresses == NULL ||
        !cJSON_IsArray(addresses)) {
        fprintf(
            stderr,
            "mnp payment list: address list missing in RPC response\n"
        );
        goto done;
    }

    size = cJSON_GetArraySize(addresses);

    for (i = 0; i < size; ++i) {
        cJSON *entry;
        cJSON *index;
        cJSON *address;

        entry = cJSON_GetArrayItem(
            addresses,
            i
        );

        if (entry == NULL) {
            goto done;
        }

        index = cJSON_GetObjectItem(
            entry,
            "address_index"
        );
        address = cJSON_GetObjectItem(
            entry,
            "address"
        );

        if (index == NULL ||
            !cJSON_IsNumber(index) ||
            address == NULL ||
            !cJSON_IsString(address) ||
            address->valuestring == NULL) {
            fprintf(
                stderr,
                "mnp payment list: invalid address entry\n"
            );
            goto done;
        }

        fprintf(
            stdout,
            "%d \"%s\"\n",
            index->valueint,
            address->valuestring
        );
    }

    status = EXIT_SUCCESS;

done:
    free_wallet(&wallet);

    return status;
}

static int run_subaddr(
    const struct Config *config,
    int index,
    const char *amount
)
{
    struct rpc_wallet wallet;
    cJSON *result;
    cJSON *addresses;
    cJSON *entry;
    char *address = NULL;
    int status = EXIT_FAILURE;

    if (init_wallet(
            &wallet,
            GET_SUBADDR,
            config
        ) == -1) {
        fprintf(
            stderr,
            "mnp payment subaddr: cannot initialize RPC request\n"
        );
        return EXIT_FAILURE;
    }

    wallet.idx = index;

    if (call_wallet(
            &wallet,
            "subaddr"
        ) == -1) {
        goto done;
    }

    result = get_result(&wallet);

    if (result == NULL) {
        fprintf(
            stderr,
            "mnp payment subaddr: invalid wallet RPC response\n"
        );
        goto done;
    }

    addresses = cJSON_GetObjectItem(
        result,
        "addresses"
    );

    if (addresses == NULL ||
        !cJSON_IsArray(addresses) ||
        cJSON_GetArraySize(addresses) < 1) {
        fprintf(
            stderr,
            "mnp payment subaddr: subaddress %d not found\n",
            index
        );
        goto done;
    }

    entry = cJSON_GetArrayItem(
        addresses,
        0
    );
    address = get_json_string(
        entry,
        "address"
    );

    if (address == NULL) {
        fprintf(
            stderr,
            "mnp payment subaddr: invalid wallet RPC response\n"
        );
        goto done;
    }

    if (amount == NULL) {
        fprintf(stdout, "%s\n", address);
        status = EXIT_SUCCESS;
        goto done;
    }

    if (print_uri(
            config,
            address,
            amount
        ) == -1) {
        goto done;
    }

    status = EXIT_SUCCESS;

done:
    free(address);
    free_wallet(&wallet);

    return status;
}

static int run_integrated(
    const struct Config *config,
    const char *payment_id,
    const char *amount
)
{
    struct rpc_wallet wallet;
    cJSON *result;
    char *integrated_address = NULL;
    int status = EXIT_FAILURE;

    if (init_wallet(
            &wallet,
            MK_IADDR,
            config
        ) == -1) {
        fprintf(
            stderr,
            "mnp payment: cannot initialize integrated-address request\n"
        );
        return EXIT_FAILURE;
    }

    wallet.payid = duplicate_string(
        payment_id,
        MAX_PAYID_SIZE
    );

    if (wallet.payid == NULL) {
        goto done;
    }

    if (call_wallet(
            &wallet,
            "payment-id"
        ) == -1) {
        goto done;
    }

    result = get_result(&wallet);

    integrated_address = get_json_string(
        result,
        "integrated_address"
    );

    if (integrated_address == NULL) {
        fprintf(
            stderr,
            "mnp payment: invalid integrated-address RPC response\n"
        );
        goto done;
    }

    if (amount == NULL) {
        fprintf(
            stdout,
            "%s\n",
            integrated_address
        );
        status = EXIT_SUCCESS;
        goto done;
    }

    if (print_uri(
            config,
            integrated_address,
            amount
        ) == -1) {
        goto done;
    }

    status = EXIT_SUCCESS;

done:
    free(integrated_address);
    free_wallet(&wallet);

    return status;
}

void payment_help(
    FILE *stream,
    const char *program
)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s payment new [--amount AMOUNT]\n"
        "  %s payment list\n"
        "  %s payment subaddr INDEX [--amount AMOUNT]\n"
        "  %s payment PAYMENT_ID [--amount AMOUNT]\n"
        "  echo PAYMENT_ID | %s payment [--amount AMOUNT]\n"
        "\n"
        "Create payment addresses and Monero payment URIs.\n"
        "\n"
        "Commands:\n"
        "  new\n"
        "      Create a new subaddress.\n"
        "\n"
        "  list\n"
        "      List all subaddresses and their indices.\n"
        "\n"
        "  subaddr INDEX\n"
        "      Print the subaddress at INDEX.\n"
        "\n"
        "Payment ID:\n"
        "  PAYMENT_ID must be exactly 16 hexadecimal characters.\n"
        "  It may be supplied as an argument or through stdin.\n"
        "  An integrated address is returned.\n"
        "\n"
        "Options:\n"
        "  --amount AMOUNT\n"
        "      Return a Monero URI containing the requested amount.\n"
        "\n"
        "Examples:\n"
        "  %s payment new\n"
        "  %s payment new --amount 650000\n"
        "  %s payment list\n"
        "  %s payment subaddr 1\n"
        "  %s payment subaddr 1 --amount 650000\n"
        "  %s payment e02c381aa2227436\n"
        "  echo e02c381aa2227436 | %s payment\n"
        "  echo e02c381aa2227436 | "
        "%s payment --amount 50000\n",
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program
    );
}

int payment_main(
    int argc,
    char **argv
)
{
    struct payment_args args;
    struct Config config = {0};
    char *config_path = NULL;
    char *payment_id = NULL;
    int status = EXIT_FAILURE;

    if (parse_arguments(
            argc,
            argv,
            &args
        ) == -1) {
        fprintf(
            stderr,
            "Try 'mnp payment help' for usage.\n"
        );
        return EXIT_FAILURE;
    }

    config_path = get_config_path();

    if (config_path == NULL) {
        fprintf(
            stderr,
            "mnp payment: cannot determine configuration path\n"
        );
        goto done;
    }

    if (ini_parse(
            config_path,
            config_handler,
            &config
        ) < 0) {
        fprintf(
            stderr,
            "mnp payment: cannot load configuration '%s'\n",
            config_path
        );
        goto done;
    }

    if (config.rpc_host == NULL ||
        config.rpc_port == NULL ||
        config.rpc_user == NULL ||
        config.rpc_password == NULL) {
        fprintf(
            stderr,
            "mnp payment: incomplete RPC configuration\n"
        );
        goto done;
    }

    switch (args.action) {
    case PAYMENT_ACTION_NEW:
        status = run_new(
            &config,
            args.amount
        );
        break;

    case PAYMENT_ACTION_LIST:
        status = run_list(&config);
        break;

    case PAYMENT_ACTION_SUBADDR:
        status = run_subaddr(
            &config,
            args.subaddress_index,
            args.amount
        );
        break;

    case PAYMENT_ACTION_INTEGRATED:
        payment_id = get_payment_id(&args);

        if (payment_id == NULL) {
            fprintf(
                stderr,
                "mnp payment: a valid 16-character "
                "payment ID is required\n"
            );
            goto done;
        }

        status = run_integrated(
            &config,
            payment_id,
            args.amount
        );
        break;

    default:
        fprintf(
            stderr,
            "mnp payment: invalid payment action\n"
        );
        break;
    }

done:
    free(payment_id);
    free(args.amount);
    free(config_path);
    free_config(&config);

    return status;
}
