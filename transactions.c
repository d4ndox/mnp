#include "transactions.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"

struct transaction_args {
    int incoming;
    int outgoing;
    int pending;
    int failed;
    int pool;
    int filtered;
};

static char *duplicate_string(const char *value, size_t maximum);
static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *get_config_path(void);
static void free_config(struct Config *config);
static int parse_arguments(int argc, char **argv, struct transaction_args *args);
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config,
                       const struct transaction_args *args);
static void free_wallet(struct rpc_wallet *wallet);
static cJSON *get_result(const struct rpc_wallet *wallet);
static void print_header(void);
static int print_transactions(const struct rpc_wallet *wallet);
static int print_transfer(const cJSON *transfer, const char *fallback_type);
static int print_transfer_array(const cJSON *result, const char *name);
static int print_destinations(const cJSON *transfer, const char *type,
                              const char *txid, long long confirmations,
                              long long height, int locked);
static const char *get_json_string(const cJSON *object, const char *name, const char *fallback);
static long long get_json_integer(const cJSON *object, const char *name, long long fallback);
static int get_json_boolean(const cJSON *object, const char *name, int fallback);
static long long get_subaddress_index(const cJSON *transfer);

/**
 * Handles the transactions subcommand.
 *
 * Loads the configured wallet RPC connection, requests selected transfer
 * categories, and prints transactions as tab-separated records.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE on error.
 */
int transactions_main(int argc, char **argv)
{
    struct transaction_args args = {0};
    struct Config config = {0};
    struct rpc_wallet wallet = {0};
    char *config_path = NULL;
    int status = EXIT_FAILURE;

    if (parse_arguments(argc, argv, &args) == -1) {
        fprintf(
            stderr,
            "Try 'mnp transactions help' for usage.\n"
        );
        return EXIT_FAILURE;
    }

    config_path = get_config_path();

    if (config_path == NULL) {
        fprintf(
            stderr,
            "mnp transactions: cannot determine configuration path\n"
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
            "mnp transactions: cannot load configuration '%s'\n",
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
            "mnp transactions: incomplete RPC configuration\n"
        );
        goto done;
    }

    if (init_wallet(
            &wallet,
            &config,
            &args
        ) == -1) {
        fprintf(
            stderr,
            "mnp transactions: cannot initialize wallet RPC request\n"
        );
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp transactions: could not connect to host %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    if (print_transactions(&wallet) == -1) {
        fprintf(
            stderr,
            "mnp transactions: invalid wallet RPC response\n"
        );
        goto done;
    }

    status = EXIT_SUCCESS;

done:
    free(config_path);
    free_wallet(&wallet);
    free_config(&config);

    return status;
}

/**
 * Prints usage information for the transactions subcommand.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in usage examples.
 */
void transactions_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s transactions [OPTIONS]\n"
        "\n"
        "List transactions known to the wallet.\n"
        "\n"
        "Options:\n"
        "  --in       Include incoming transactions.\n"
        "  --out      Include outgoing transactions.\n"
        "  --pending  Include pending outgoing transactions.\n"
        "  --failed   Include failed outgoing transactions.\n"
        "  --pool     Include incoming transactions in the txpool.\n"
        "  -h, --help Show this help.\n"
        "\n"
        "Without filters all transaction types are included.\n"
        "\n"
        "Output:\n"
        "  TYPE<TAB>TXID<TAB>AMOUNT<TAB>CONFIRMATIONS<TAB>HEIGHT<TAB>LOCKED\n",
        program
    );
}

/**
 * Duplicates a string while enforcing a maximum accepted length.
 *
 * @param value The string to duplicate.
 * @param maximum The maximum accepted string length.
 * @return A dynamically allocated copy, or NULL on failure.
 */
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

/**
 * Parses transaction-related configuration values.
 *
 * @param user A pointer to the Config structure receiving parsed values.
 * @param section The configuration section name.
 * @param name The configuration option name.
 * @param value The configuration option value.
 * @return 1 if the entry was handled, or 0 otherwise.
 */
static int config_handler(void *user, const char *section, const char *name, const char *value)
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

/**
 * Builds the path to the mnp configuration file.
 *
 * @return A dynamically allocated path, or NULL on failure.
 */
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

/**
 * Releases dynamically allocated configuration values.
 *
 * @param config A pointer to the Config structure.
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
    free((void *)config->mnp_account);
}

/**
 * Parses transactions command-line options.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @param args A pointer receiving the parsed filters.
 * @return 0 on success, or -1 if arguments are invalid.
 */
static int parse_arguments(int argc, char **argv, struct transaction_args *args)
{
    int i;

    if (args == NULL) {
        return -1;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--in") == 0) {
            args->incoming = 1;
            args->filtered = 1;
        } else if (strcmp(argv[i], "--out") == 0) {
            args->outgoing = 1;
            args->filtered = 1;
        } else if (strcmp(argv[i], "--pending") == 0) {
            args->pending = 1;
            args->filtered = 1;
        } else if (strcmp(argv[i], "--failed") == 0) {
            args->failed = 1;
            args->filtered = 1;
        } else if (strcmp(argv[i], "--pool") == 0) {
            args->pool = 1;
            args->filtered = 1;
        } else if (strcmp(argv[i], "help") == 0 ||
                   strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            transactions_help(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        } else {
            fprintf(
                stderr,
                "mnp transactions: unknown option '%s'\n",
                argv[i]
            );
            return -1;
        }
    }

    if (!args->filtered) {
        args->incoming = 1;
        args->outgoing = 1;
        args->pending = 1;
        args->failed = 1;
        args->pool = 1;
    }

    return 0;
}

/**
 * Initializes a get_transfers wallet RPC request.
 *
 * @param wallet The wallet RPC structure to initialize.
 * @param config The loaded mnp configuration.
 * @param args The selected transaction filters.
 * @return 0 on success, or -1 on failure.
 */
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config,
                       const struct transaction_args *args)
{
    if (wallet == NULL ||
        config == NULL ||
        args == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    wallet->monero_rpc_method = GET_TRANSFERS;
    wallet->host = duplicate_string(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = duplicate_string(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = duplicate_string(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = duplicate_string(config->rpc_password, MAX_DATA_SIZE);
    wallet->account = duplicate_string(
        config->mnp_account != NULL ? config->mnp_account : "0",
        MAX_DATA_SIZE
    );

    wallet->transactions_in = args->incoming;
    wallet->transactions_out = args->outgoing;
    wallet->transactions_pending = args->pending;
    wallet->transactions_failed = args->failed;
    wallet->transactions_pool = args->pool;

    if (wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL ||
        wallet->account == NULL) {
        return -1;
    }

    return 0;
}

/**
 * Releases dynamically allocated wallet RPC fields.
 *
 * @param wallet The wallet structure to clean up.
 */
static void free_wallet(struct rpc_wallet *wallet)
{
    if (wallet == NULL) {
        return;
    }

    free(wallet->host);
    free(wallet->port);
    free(wallet->user);
    free(wallet->pwd);
    free(wallet->account);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

/**
 * Retrieves the result object from a JSON-RPC response.
 *
 * @param wallet The completed wallet RPC request.
 * @return The result object, or NULL if absent or malformed.
 */
static cJSON *get_result(const struct rpc_wallet *wallet)
{
    cJSON *result;

    if (wallet == NULL || wallet->reply == NULL) {
        return NULL;
    }

    result = cJSON_GetObjectItemCaseSensitive(
        wallet->reply,
        "result"
    );

    if (result == NULL || !cJSON_IsObject(result)) {
        return NULL;
    }

    return result;
}

/**
 * Prints the transaction table header.
 */
static void print_header(void)
{
    fprintf(
        stdout,
        "TYPE\tTXID\tAMOUNT\tCONFIRMATIONS\tHEIGHT\tLOCKED\tADDRESS\tSUBADDR_INDEX\n"
    );
}

/**
 * Prints all transaction categories from a get_transfers response.
 *
 * @param wallet The completed wallet RPC request.
 * @return 0 on success, or -1 for a malformed response.
 */
static int print_transactions(const struct rpc_wallet *wallet)
{
    cJSON *result;

    result = get_result(wallet);

    if (result == NULL) {
        return -1;
    }

    print_header();

    if (print_transfer_array(result, "in") == -1 ||
        print_transfer_array(result, "out") == -1 ||
        print_transfer_array(result, "pending") == -1 ||
        print_transfer_array(result, "failed") == -1 ||
        print_transfer_array(result, "pool") == -1) {
        return -1;
    }

    return 0;
}

/**
 * Prints one transfer as one or more tab-separated records.
 *
 * Outgoing transfers use destinations[] when available so the actual
 * recipient addresses are shown. Incoming transfers use address and
 * subaddr_index from the transfer object.
 *
 * @param transfer The transfer JSON object.
 * @param fallback_type The enclosing transfer category.
 * @return 0 on success, or -1 if the transfer is malformed.
 */
static int print_transfer(const cJSON *transfer, const char *fallback_type)
{
    const cJSON *destinations;
    const cJSON *destination;
    const char *type;
    const char *txid;
    const char *address;
    long long amount;
    long long confirmations;
    long long height;
    long long subaddress_index;
    int locked;
    int destination_count = 0;

    if (transfer == NULL ||
        !cJSON_IsObject(transfer)) {
        return -1;
    }

    type = fallback_type;

    txid = get_json_string(
        transfer,
        "txid",
        "-"
    );

    amount = get_json_integer(
        transfer,
        "amount",
        0
    );

    confirmations = get_json_integer(
        transfer,
        "confirmations",
        0
    );

    height = get_json_integer(
        transfer,
        "height",
        0
    );

    locked = get_json_boolean(
        transfer,
        "locked",
        0
    );

    if (strcmp(type, "out") == 0) {
        destinations = cJSON_GetObjectItemCaseSensitive(
            transfer,
            "destinations"
        );

        if (destinations != NULL &&
            cJSON_IsArray(destinations)) {
            cJSON_ArrayForEach(destination, destinations) {
                const char *destination_address;
                long long destination_amount;

                if (!cJSON_IsObject(destination)) {
                    return -1;
                }

                destination_address = get_json_string(
                    destination,
                    "address",
                    "-"
                );

                destination_amount = get_json_integer(
                    destination,
                    "amount",
                    0
                );

                fprintf(
                    stdout,
                    "%s\t%s\t%lld\t%lld\t%lld\t%s\t%s\t-\n",
                    type,
                    txid,
                    destination_amount,
                    confirmations,
                    height,
                    locked ? "true" : "false",
                    destination_address
                );

                destination_count++;
            }
        }

        if (destination_count > 0) {
            return 0;
        }

        fprintf(
            stdout,
            "%s\t%s\t%lld\t%lld\t%lld\t%s\t-\t-\n",
            type,
            txid,
            amount,
            confirmations,
            height,
            locked ? "true" : "false"
        );

        return 0;
    }

    address = get_json_string(
        transfer,
        "address",
        "-"
    );

    subaddress_index = get_subaddress_index(
        transfer
    );

    fprintf(
        stdout,
        "%s\t%s\t%lld\t%lld\t%lld\t%s\t%s\t%lld\n",
        type,
        txid,
        amount,
        confirmations,
        height,
        locked ? "true" : "false",
        address,
        subaddress_index
    );

    return 0;
}

static int print_transfer_array(const cJSON *result, const char *name)
{
    const cJSON *array;
    const cJSON *transfer;

    array = cJSON_GetObjectItemCaseSensitive(
        result,
        name
    );

    if (array == NULL) {
        return 0;
    }

    if (!cJSON_IsArray(array)) {
        return -1;
    }

    cJSON_ArrayForEach(transfer, array) {
        if (print_transfer(
                transfer,
                name
            ) == -1) {
            return -1;
        }
    }

    return 0;
}

/**
 * Reads a string value from a JSON object.
 *
 * @param object The JSON object.
 * @param name The property name.
 * @param fallback The value returned if the property is unavailable.
 * @return The string property or the fallback.
 */
static const char *get_json_string(const cJSON *object, const char *name, const char *fallback)
{
    const cJSON *value;

    value = cJSON_GetObjectItemCaseSensitive(
        object,
        name
    );

    if (value == NULL ||
        !cJSON_IsString(value) ||
        value->valuestring == NULL) {
        return fallback;
    }

    return value->valuestring;
}

/**
 * Reads an integer value from a JSON object.
 *
 * @param object The JSON object.
 * @param name The property name.
 * @param fallback The value returned if the property is unavailable.
 * @return The integer property or the fallback.
 */
static long long get_json_integer(const cJSON *object, const char *name, long long fallback)
{
    const cJSON *value;

    value = cJSON_GetObjectItemCaseSensitive(
        object,
        name
    );

    if (value == NULL ||
        !cJSON_IsNumber(value)) {
        return fallback;
    }

    return (long long)value->valuedouble;
}

/**
 * Reads a boolean value from a JSON object.
 *
 * @param object The JSON object.
 * @param name The property name.
 * @param fallback The value returned if the property is unavailable.
 * @return 1 for true, 0 for false, or the fallback.
 */
static int get_json_boolean(const cJSON *object, const char *name, int fallback)
{
    const cJSON *value;

    value = cJSON_GetObjectItemCaseSensitive(
        object,
        name
    );

    if (value == NULL ||
        !cJSON_IsBool(value)) {
        return fallback;
    }

    return cJSON_IsTrue(value) ? 1 : 0;
}

/**
 * Reads the minor index from a Monero subaddress index object.
 *
 * @param transfer The transfer JSON object.
 * @return The minor subaddress index, or 0 if unavailable.
 */
static long long get_subaddress_index(const cJSON *transfer)
{
    const cJSON *subaddr_index;
    const cJSON *minor;

    if (transfer == NULL) {
        return 0;
    }

    subaddr_index = cJSON_GetObjectItemCaseSensitive(
        transfer,
        "subaddr_index"
    );

    if (subaddr_index == NULL ||
        !cJSON_IsObject(subaddr_index)) {
        return 0;
    }

    minor = cJSON_GetObjectItemCaseSensitive(
        subaddr_index,
        "minor"
    );

    if (minor == NULL ||
        !cJSON_IsNumber(minor)) {
        return 0;
    }

    return (long long)minor->valuedouble;
}

static int print_destinations(const cJSON *transfer, const char *type,
                              const char *txid, long long confirmations,
                              long long height, int locked)
{
    const cJSON *destinations;
    const cJSON *destination;

    destinations = cJSON_GetObjectItemCaseSensitive(
        transfer,
        "destinations"
    );

    if (destinations == NULL ||
        !cJSON_IsArray(destinations)) {
        return 0;
    }

    cJSON_ArrayForEach(destination, destinations) {
        const char *address;
        long long amount;

        address = get_json_string(
            destination,
            "address",
            "-"
        );

        amount = get_json_integer(
            destination,
            "amount",
            0
        );

        fprintf(
            stdout,
            "%s\t%s\t%lld\t%lld\t%lld\t%s\t%s\t-\n",
            type,
            txid,
            amount,
            confirmations,
            height,
            locked ? "true" : "false",
            address
        );
    }

    return 1;
}
