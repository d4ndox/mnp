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

/*
 * transfer.c
 */

#include "transfer.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"
#include "validate.h"

struct transfer_destination {
    char *address;
    unsigned long long amount;
    int has_payment_id;
};

static char *duplicate_string(const char *value, size_t maximum);
static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *get_config_path(void);
static void free_config(struct Config *config);
static int init_wallet(struct rpc_wallet *wallet, enum monero_rpc_method method,
                       const struct Config *config);
static void free_wallet(struct rpc_wallet *wallet);
static int call_wallet(struct rpc_wallet *wallet, const char *command);
static cJSON *get_result(const struct rpc_wallet *wallet);
static int uri_has_payment_id(const char *uri, const char *address);
static int parse_amount(const cJSON *object, unsigned long long *amount);
static int parse_transfer_uri(const struct Config *config, const char *uri,
                              struct transfer_destination *destination);
static int parse_atomic_amount(const char *value, unsigned long long *amount);
static int parse_address_arguments(int argc, char **argv,
                                   struct transfer_destination **destinations,
                                   size_t *destination_count);
static int parse_uri_arguments(const struct Config *config, int argc, char **argv,
                               struct transfer_destination **destinations,
                               size_t *destination_count);
static int validate_payment_ids(const struct transfer_destination *destinations, size_t count);
static void free_destinations(struct transfer_destination *destinations, size_t count);
static char *build_destinations_json(const struct transfer_destination *destinations, size_t count);
static int run_transfer(const struct Config *config,
                        const struct transfer_destination *destinations, size_t count);
static char *extract_tx_hash(const struct rpc_wallet *wallet);

/**
 * Handles the transfer subcommand.
 *
 * Supports either Monero payment URIs or repeated address/amount pairs.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE on error.
 */
int transfer_main(int argc, char **argv)
{
    struct Config config = {0};
    struct transfer_destination *destinations = NULL;
    char *config_path = NULL;
    size_t destination_count = 0;
    int address_mode;
    int status = EXIT_FAILURE;

    if (argc == 2 &&
        (strcmp(argv[1], "help") == 0 ||
         strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "-h") == 0)) {
        transfer_help(stdout, argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc < 2) {
        fprintf(stderr, "Try 'mnp transfer help' for usage.\n");
        return EXIT_FAILURE;
    }

    config_path = get_config_path();

    if (config_path == NULL) {
        fprintf(stderr, "mnp transfer: cannot determine configuration path\n");
        goto done;
    }

    if (ini_parse(config_path, config_handler, &config) < 0) {
        fprintf(stderr, "mnp transfer: cannot load configuration '%s'\n", config_path);
        goto done;
    }

    if (config.rpc_host == NULL ||
        config.rpc_port == NULL ||
        config.rpc_user == NULL ||
        config.rpc_password == NULL) {
        fprintf(stderr, "mnp transfer: incomplete RPC configuration\n");
        goto done;
    }

    address_mode = strcmp(argv[1], "--address") == 0;

    if (address_mode) {
        if (parse_address_arguments(
                argc,
                argv,
                &destinations,
                &destination_count
            ) == -1) {
            goto done;
        }
    } else {
        if (parse_uri_arguments(
                &config,
                argc,
                argv,
                &destinations,
                &destination_count
            ) == -1) {
            goto done;
        }
    }

    if (validate_payment_ids(destinations, destination_count) == -1) {
        goto done;
    }

    if (run_transfer(
            &config,
            destinations,
            destination_count
        ) == -1) {
        goto done;
    }

    status = EXIT_SUCCESS;

done:
    free(config_path);
    free_destinations(destinations, destination_count);
    free(destinations);
    free_config(&config);

    return status;
}

/**
 * Prints usage information for the transfer subcommand.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in usage examples.
 */
void transfer_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s transfer URI [URI ...]\n"
        "  %s transfer --address ADDRESS --amount AMOUNT\n"
        "               [--address ADDRESS --amount AMOUNT ...]\n"
        "\n"
        "Send Monero to one or more destinations.\n"
        "\n"
        "URI examples:\n"
        "  %s transfer 'monero:ADDRESS?tx_amount=0.000000066565'\n"
        "\n"
        "  %s transfer \\\n"
        "      'monero:ADDRESS1?tx_amount=0.000000483949' \\\n"
        "      'monero:ADDRESS2?tx_amount=0.000000000006'\n"
        "\n"
        "Address examples:\n"
        "  %s transfer --address ADDRESS --amount 66565\n"
        "\n"
        "  %s transfer \\\n"
        "      --address ADDRESS1 --amount 483949 \\\n"
        "      --address ADDRESS2 --amount 6\n"
        "\n"
        "Amounts passed with --amount are atomic units.\n"
        "A payment-ID destination must be the only destination.\n"
        "\n"
        "Output:\n"
        "  TXID\n",
        program,
        program,
        program,
        program,
        program,
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
 * Parses RPC and account configuration values.
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
 * @return A dynamically allocated configuration path, or NULL on failure.
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

    if (snprintf(path, length, "%s/%s", home, CONFIG_FILE) < 0) {
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
 * Initializes a wallet RPC request.
 *
 * @param wallet The wallet RPC structure to initialize.
 * @param method The wallet RPC method to use.
 * @param config The loaded mnp configuration.
 * @return 0 on success, or -1 on failure.
 */
static int init_wallet(struct rpc_wallet *wallet, enum monero_rpc_method method,
                       const struct Config *config)
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

    account = config->mnp_account != NULL
        ? config->mnp_account
        : "0";

    wallet->monero_rpc_method = method;
    wallet->account = duplicate_string(account, MAX_DATA_SIZE);
    wallet->host = duplicate_string(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = duplicate_string(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = duplicate_string(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = duplicate_string(config->rpc_password, MAX_DATA_SIZE);

    if (wallet->account == NULL ||
        wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL) {
        free_wallet(wallet);
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

    free(wallet->params);
    free(wallet->account);
    free(wallet->host);
    free(wallet->port);
    free(wallet->user);
    free(wallet->pwd);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }

    memset(wallet, 0, sizeof(*wallet));
}

/**
 * Executes a wallet RPC request.
 *
 * @param wallet The configured wallet RPC request.
 * @param command The command name used in error messages.
 * @return 0 on success, or -1 on RPC failure.
 */
static int call_wallet(struct rpc_wallet *wallet, const char *command)
{
    if (rpc_call(wallet) < 0) {
        fprintf(stderr, "mnp transfer %s: wallet RPC request failed\n", command);
        return -1;
    }

    return 0;
}

/**
 * Retrieves the JSON-RPC result object.
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

    result = cJSON_GetObjectItemCaseSensitive(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return NULL;
    }

    return result;
}

/**
 * Detects whether a URI carries a transaction payment ID.
 *
 * @param uri The original Monero payment URI.
 * @param address The parsed destination address.
 * @return 1 if a payment ID is present, or 0 otherwise.
 */
static int uri_has_payment_id(const char *uri, const char *address)
{
    if (uri == NULL || address == NULL) {
        return 0;
    }

    if (strlen(address) == 106) {
        return 1;
    }

    return strstr(uri, "tx_payment_id=") != NULL;
}

/**
 * Extracts a positive atomic-unit amount from a parsed URI object.
 *
 * @param object The parsed URI object.
 * @param amount A pointer receiving the atomic-unit amount.
 * @return 0 on success, or -1 if the amount is missing or invalid.
 */
static int parse_amount(const cJSON *object, unsigned long long *amount)
{
    const cJSON *item;
    double value;

    if (object == NULL || amount == NULL) {
        return -1;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, "amount");

    if (item == NULL || !cJSON_IsNumber(item)) {
        return -1;
    }

    value = item->valuedouble;

    if (value < 1.0 || value > (double)ULLONG_MAX) {
        return -1;
    }

    *amount = (unsigned long long)value;

    if ((double)*amount != value) {
        return -1;
    }

    return 0;
}

/**
 * Parses one Monero payment URI through monero-wallet-rpc.
 *
 * @param config The loaded mnp RPC configuration.
 * @param uri The Monero payment URI.
 * @param destination The destination receiving parsed values.
 * @return 0 on success, or -1 on failure.
 */
static int parse_transfer_uri(const struct Config *config, const char *uri,
                              struct transfer_destination *destination)
{
    struct rpc_wallet wallet;
    cJSON *result;
    cJSON *uri_object;
    cJSON *address;
    int status = -1;

    if (config == NULL || uri == NULL || destination == NULL) {
        return -1;
    }

    memset(destination, 0, sizeof(*destination));

    if (init_wallet(&wallet, PARSE_URI, config) == -1) {
        fprintf(stderr, "mnp transfer: cannot initialize parse_uri request\n");
        return -1;
    }

    wallet.params = duplicate_string(uri, MAX_DATA_SIZE);

    if (wallet.params == NULL) {
        fprintf(stderr, "mnp transfer: URI is too long\n");
        goto done;
    }

    if (call_wallet(&wallet, "parse_uri") == -1) {
        goto done;
    }

    result = get_result(&wallet);

    if (result == NULL) {
        fprintf(stderr, "mnp transfer: invalid parse_uri response\n");
        goto done;
    }

    uri_object = cJSON_GetObjectItemCaseSensitive(result, "uri");

    if (uri_object == NULL || !cJSON_IsObject(uri_object)) {
        fprintf(stderr, "mnp transfer: invalid Monero URI\n");
        goto done;
    }

    address = cJSON_GetObjectItemCaseSensitive(uri_object, "address");

    if (address == NULL ||
        !cJSON_IsString(address) ||
        address->valuestring == NULL ||
        address->valuestring[0] == '\0') {
        fprintf(stderr, "mnp transfer: URI contains no valid address\n");
        goto done;
    }

    destination->address = duplicate_string(address->valuestring, MAX_IADDR_SIZE);

    if (destination->address == NULL) {
        fprintf(stderr, "mnp transfer: invalid destination address\n");
        goto done;
    }

    if (parse_amount(uri_object, &destination->amount) == -1) {
        fprintf(stderr, "mnp transfer: URI amount is required\n");
        goto done;
    }

    destination->has_payment_id = uri_has_payment_id(
        uri,
        destination->address
    );

    status = 0;

done:
    if (status == -1) {
        free(destination->address);
        memset(destination, 0, sizeof(*destination));
    }

    free_wallet(&wallet);

    return status;
}

/**
 * Parses an atomic-unit amount.
 *
 * @param value The decimal amount string.
 * @param amount A pointer receiving the parsed amount.
 * @return 0 on success, or -1 on invalid input.
 */
static int parse_atomic_amount(const char *value, unsigned long long *amount)
{
    char *end = NULL;
    unsigned long long parsed;

    if (value == NULL ||
        amount == NULL ||
        val_amount(value) < 0 ||
        strcmp(value, "0") == 0) {
        return -1;
    }

    errno = 0;
    parsed = strtoull(value, &end, 10);

    if (errno == ERANGE ||
        end == value ||
        *end != '\0' ||
        parsed == 0) {
        return -1;
    }

    *amount = parsed;

    return 0;
}

/**
 * Parses repeated --address ADDRESS --amount AMOUNT pairs.
 *
 * @param argc The argument count.
 * @param argv The argument vector.
 * @param destinations A pointer receiving the allocated destinations.
 * @param destination_count A pointer receiving the destination count.
 * @return 0 on success, or -1 on invalid input.
 */
static int parse_address_arguments(int argc, char **argv,
                                   struct transfer_destination **destinations,
                                   size_t *destination_count)
{
    struct transfer_destination *items;
    size_t count;
    size_t i;

    if (argc < 5 ||
        destinations == NULL ||
        destination_count == NULL) {
        fprintf(stderr, "mnp transfer: expected --address ADDRESS --amount AMOUNT\n");
        return -1;
    }

    if ((argc - 1) % 4 != 0) {
        fprintf(stderr, "mnp transfer: incomplete address/amount pair\n");
        return -1;
    }

    count = (size_t)(argc - 1) / 4;

    items = calloc(count, sizeof(*items));

    if (items == NULL) {
        fprintf(stderr, "mnp transfer: out of memory\n");
        return -1;
    }

    for (i = 0; i < count; i++) {
        int index = 1 + (int)(i * 4);
        const char *address;
        const char *amount;

        if (strcmp(argv[index], "--address") != 0 ||
            strcmp(argv[index + 2], "--amount") != 0) {
            fprintf(
                stderr,
                "mnp transfer: expected --address ADDRESS --amount AMOUNT\n"
            );
            goto error;
        }

        address = argv[index + 1];
        amount = argv[index + 3];

        if (val_address(address) < 0) {
            fprintf(stderr, "mnp transfer: invalid address '%s'\n", address);
            goto error;
        }

        if (parse_atomic_amount(amount, &items[i].amount) == -1) {
            fprintf(stderr, "mnp transfer: invalid amount '%s'\n", amount);
            goto error;
        }

        items[i].address = duplicate_string(address, MAX_IADDR_SIZE);

        if (items[i].address == NULL) {
            fprintf(stderr, "mnp transfer: cannot store address\n");
            goto error;
        }

        items[i].has_payment_id = strlen(address) == 106;
    }

    *destinations = items;
    *destination_count = count;

    return 0;

error:
    free_destinations(items, count);
    free(items);

    return -1;
}

/**
 * Parses one or more Monero payment URI arguments.
 *
 * @param config The loaded mnp RPC configuration.
 * @param argc The argument count.
 * @param argv The argument vector.
 * @param destinations A pointer receiving the allocated destinations.
 * @param destination_count A pointer receiving the destination count.
 * @return 0 on success, or -1 on invalid input.
 */
static int parse_uri_arguments(const struct Config *config, int argc, char **argv,
                               struct transfer_destination **destinations,
                               size_t *destination_count)
{
    struct transfer_destination *items;
    size_t count;
    size_t i;

    if (config == NULL ||
        destinations == NULL ||
        destination_count == NULL ||
        argc < 2) {
        return -1;
    }

    count = (size_t)(argc - 1);

    items = calloc(count, sizeof(*items));

    if (items == NULL) {
        fprintf(stderr, "mnp transfer: out of memory\n");
        return -1;
    }

    for (i = 0; i < count; i++) {
        const char *uri = argv[i + 1];

        if (strncmp(uri, "monero:", 7) != 0) {
            if (strncmp(uri, "--", 2) == 0) {
                fprintf(stderr, "mnp transfer: URI and address modes cannot be mixed\n");
            } else {
                fprintf(stderr, "mnp transfer: invalid Monero URI '%s'\n", uri);
            }

            goto error;
        }

        if (parse_transfer_uri(
                config,
                uri,
                &items[i]
            ) == -1) {
            goto error;
        }
    }

    *destinations = items;
    *destination_count = count;

    return 0;

error:
    free_destinations(items, count);
    free(items);

    return -1;
}

/**
 * Ensures that a payment-ID destination is transferred separately.
 *
 * @param destinations The parsed destinations.
 * @param count The destination count.
 * @return 0 if valid, or -1 if the transfer is unsupported.
 */
static int validate_payment_ids(const struct transfer_destination *destinations, size_t count)
{
    size_t payment_id_count = 0;
    size_t i;

    if (destinations == NULL || count == 0) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (destinations[i].has_payment_id) {
            payment_id_count++;
        }
    }

    if (count > 1 && payment_id_count > 0) {
        fprintf(stderr, "mnp transfer: payment ID requires a single destination\n");
        return -1;
    }

    return 0;
}

/**
 * Releases parsed destination addresses.
 *
 * @param destinations The destination array.
 * @param count The number of array elements.
 */
static void free_destinations(struct transfer_destination *destinations, size_t count)
{
    size_t i;

    if (destinations == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        free(destinations[i].address);
    }
}

/**
 * Serializes transfer destinations as a JSON array.
 *
 * @param destinations The parsed transfer destinations.
 * @param count The number of destinations.
 * @return A dynamically allocated JSON string, or NULL on failure.
 */
static char *build_destinations_json(const struct transfer_destination *destinations, size_t count)
{
    cJSON *array;
    size_t i;
    char *json = NULL;

    if (destinations == NULL || count == 0) {
        return NULL;
    }

    array = cJSON_CreateArray();

    if (array == NULL) {
        return NULL;
    }

    for (i = 0; i < count; i++) {
        cJSON *object;

        object = cJSON_CreateObject();

        if (object == NULL) {
            goto done;
        }

        if (cJSON_AddStringToObject(
                object,
                "address",
                destinations[i].address
            ) == NULL) {
            cJSON_Delete(object);
            goto done;
        }

        if (cJSON_AddNumberToObject(
                object,
                "amount",
                (double)destinations[i].amount
            ) == NULL) {
            cJSON_Delete(object);
            goto done;
        }

        cJSON_AddItemToArray(array, object);
    }

    json = cJSON_PrintUnformatted(array);

done:
    cJSON_Delete(array);

    return json;
}

/**
 * Sends all parsed destinations in one wallet transfer.
 *
 * @param config The loaded mnp RPC configuration.
 * @param destinations The parsed transfer destinations.
 * @param count The number of destinations.
 * @return 0 on success, or -1 on failure.
 */
static int run_transfer(const struct Config *config,
                        const struct transfer_destination *destinations, size_t count)
{
    struct rpc_wallet wallet;
    char *tx_hash = NULL;
    int status = -1;

    if (init_wallet(&wallet, TRANSFER, config) == -1) {
        fprintf(stderr, "mnp transfer: cannot initialize transfer request\n");
        return -1;
    }

    wallet.params = build_destinations_json(destinations, count);

    if (wallet.params == NULL) {
        fprintf(stderr, "mnp transfer: cannot create destinations\n");
        goto done;
    }

    if (call_wallet(&wallet, "transfer") == -1) {
        goto done;
    }

    tx_hash = extract_tx_hash(&wallet);

    if (tx_hash == NULL) {
        fprintf(stderr, "mnp transfer: invalid transfer response\n");
        goto done;
    }

    fprintf(stdout, "%s\n", tx_hash);

    status = 0;

done:
    free(tx_hash);
    free_wallet(&wallet);

    return status;
}

/**
 * Extracts the transaction hash from a transfer RPC response.
 *
 * @param wallet The completed transfer RPC request.
 * @return A dynamically allocated transaction hash, or NULL on failure.
 */
static char *extract_tx_hash(const struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *tx_hash;

    result = get_result(wallet);

    if (result == NULL) {
        return NULL;
    }

    tx_hash = cJSON_GetObjectItemCaseSensitive(result, "tx_hash");

    if (tx_hash == NULL ||
        !cJSON_IsString(tx_hash) ||
        tx_hash->valuestring == NULL) {
        return NULL;
    }

    return duplicate_string(tx_hash->valuestring, 64);
}
