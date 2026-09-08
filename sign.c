#include "sign.h"

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

struct sign_args {
    const char *message_argument;
    int subaddress_index;
};

static char *duplicate_string(const char *value, size_t maximum);
static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *get_config_path(void);
static void free_config(struct Config *config);
static int parse_nonnegative_int(const char *value, int *result);
static int parse_arguments(int argc, char **argv, struct sign_args *args);
static char *read_stdin(void);
static char *get_message(const struct sign_args *args);
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config, int subaddress_index);
static void free_wallet(struct rpc_wallet *wallet);
static cJSON *get_result(const struct rpc_wallet *wallet);
static char *get_signature(const struct rpc_wallet *wallet);

/**
 * Signs arbitrary data with the configured Monero wallet view key.
 *
 * The data may be supplied as a positional argument or through standard input.
 * Exactly one trailing LF is removed from standard input.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE on error.
 */
int sign_main(int argc, char **argv)
{
    struct sign_args args = {0};
    struct Config config = {0};
    struct rpc_wallet wallet = {0};
    char *config_path = NULL;
    char *message = NULL;
    char *signature = NULL;
    int result = EXIT_FAILURE;

    if (parse_arguments(argc, argv, &args) == -1) {
        fprintf(
            stderr,
            "Try 'mnp sign help' for usage.\n"
        );
        return EXIT_FAILURE;
    }

    message = get_message(&args);

    if (message == NULL) {
        fprintf(stderr, "mnp sign: message is required\n");
        goto done;
    }

    config_path = get_config_path();

    if (config_path == NULL) {
        fprintf(stderr, "mnp sign: cannot determine configuration path\n");
        goto done;
    }

    if (ini_parse(config_path, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp sign: cannot load configuration '%s'\n",
            config_path
        );
        goto done;
    }

    if (init_wallet(
            &wallet,
            &config,
            args.subaddress_index
        ) == -1) {
        fprintf(stderr, "mnp sign: incomplete RPC configuration\n");
        goto done;
    }

    wallet.data = duplicate_string(message, MAX_DATA_SIZE);

    if (wallet.data == NULL) {
        fprintf(stderr, "mnp sign: message is too long\n");
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp sign: could not connect to host %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    signature = get_signature(&wallet);

    if (signature == NULL) {
        fprintf(stderr, "mnp sign: invalid wallet RPC response\n");
        goto done;
    }

    fprintf(stdout, "%s\n", signature);
    result = EXIT_SUCCESS;

done:
    free(signature);
    free(message);
    free(config_path);
    free_wallet(&wallet);
    free_config(&config);

    return result;
}

/**
 * Prints usage information for the sign command.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in usage examples.
 */
void sign_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s sign [--subaddr INDEX] MESSAGE\n"
        "  echo MESSAGE | %s sign [--subaddr INDEX]\n"
        "\n"
        "Sign arbitrary data using the wallet view key.\n"
        "\n"
        "Options:\n"
        "  --subaddr INDEX\n"
        "      Sign using the specified subaddress index.\n"
        "      The configured account is used.\n"
        "\n"
        "Input:\n"
        "  If MESSAGE is omitted, data is read from stdin.\n"
        "  Exactly one trailing LF is removed from stdin.\n",
        program,
        program
    );
}

/**
 * Duplicates a string while enforcing a maximum accepted length.
 *
 * @param value The string to duplicate.
 * @param maximum The maximum accepted string length.
 * @return A newly allocated string, or NULL on failure.
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
 * @param user A pointer to the Config structure.
 * @param section The configuration section.
 * @param name The configuration key.
 * @param value The configuration value.
 * @return 1 if handled, or 0 otherwise.
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
 * @return A newly allocated path, or NULL on failure.
 */
static char *get_config_path(void)
{
    const char *home;
    const struct passwd *entry;
    size_t length;
    char *path;

    home = getenv("HOME");

    if (home == NULL) {
        entry = getpwuid(getuid());

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
 * Releases dynamically allocated configuration fields.
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
 * Parses a non-negative integer.
 *
 * @param value The numeric string.
 * @param result A pointer receiving the parsed value.
 * @return 0 on success, or -1 on failure.
 */
static int parse_nonnegative_int(const char *value, int *result)
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
        parsed < 0 ||
        parsed > INT_MAX) {
        return -1;
    }

    *result = (int)parsed;

    return 0;
}

/**
 * Parses sign command arguments.
 *
 * @param argc The number of arguments.
 * @param argv The argument vector.
 * @param args A pointer receiving parsed arguments.
 * @return 0 on success, or -1 on invalid arguments.
 */
static int parse_arguments(int argc, char **argv, struct sign_args *args)
{
    int i;

    if (args == NULL) {
        return -1;
    }

    args->message_argument = NULL;
    args->subaddress_index = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "help") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0) {
            sign_help(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[i], "--subaddr") == 0) {
            if (++i >= argc ||
                parse_nonnegative_int(
                    argv[i],
                    &args->subaddress_index
                ) == -1) {
                fprintf(stderr, "mnp sign: invalid subaddress index\n");
                return -1;
            }

            continue;
        }

        if (args->message_argument != NULL) {
            fprintf(stderr, "mnp sign: too many arguments\n");
            return -1;
        }

        args->message_argument = argv[i];
    }

    return 0;
}

/**
 * Reads all data from stdin and removes exactly one trailing LF.
 *
 * @return A newly allocated string, or NULL on failure.
 */
static char *read_stdin(void)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int character;

    while ((character = fgetc(stdin)) != EOF) {
        char *resized;

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity == 0 ? 256 : capacity * 2;

            if (new_capacity > MAX_DATA_SIZE + 1) {
                new_capacity = MAX_DATA_SIZE + 1;
            }

            if (new_capacity <= capacity) {
                free(buffer);
                return NULL;
            }

            resized = realloc(buffer, new_capacity);

            if (resized == NULL) {
                free(buffer);
                return NULL;
            }

            buffer = resized;
            capacity = new_capacity;
        }

        if (length >= MAX_DATA_SIZE) {
            free(buffer);
            return NULL;
        }

        buffer[length++] = (char)character;
    }

    if (ferror(stdin)) {
        free(buffer);
        return NULL;
    }

    if (buffer == NULL) {
        return NULL;
    }

    if (length > 0 && buffer[length - 1] == '\n') {
        --length;
    }

    buffer[length] = '\0';

    return buffer;
}

/**
 * Obtains the message from an argument or stdin.
 *
 * @param args Parsed sign arguments.
 * @return A newly allocated message, or NULL on failure.
 */
static char *get_message(const struct sign_args *args)
{
    if (args->message_argument != NULL) {
        return duplicate_string(
            args->message_argument,
            MAX_DATA_SIZE
        );
    }

    if (isatty(STDIN_FILENO)) {
        return NULL;
    }

    return read_stdin();
}

/**
 * Initializes a wallet RPC request for signing.
 *
 * @param wallet A pointer to the wallet structure.
 * @param config A pointer to the loaded configuration.
 * @param subaddress_index The address index to use.
 * @return 0 on success, or -1 on failure.
 */
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config, int subaddress_index)
{
    if (wallet == NULL ||
        config == NULL ||
        config->rpc_host == NULL ||
        config->rpc_port == NULL ||
        config->rpc_user == NULL ||
        config->rpc_password == NULL) {
        return -1;
    }

    memset(wallet, 0, sizeof(*wallet));

    wallet->monero_rpc_method = SIGN_MESSAGE;
    wallet->host = duplicate_string(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = duplicate_string(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = duplicate_string(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = duplicate_string(config->rpc_password, MAX_DATA_SIZE);
    wallet->account = duplicate_string(
        config->mnp_account != NULL ? config->mnp_account : "0",
        MAX_DATA_SIZE
    );
    wallet->idx = subaddress_index;

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
 * Releases fields allocated for a wallet request.
 *
 * @param wallet A pointer to the wallet structure.
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
    free(wallet->data);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

/**
 * Retrieves the JSON-RPC result object.
 *
 * @param wallet A pointer to the wallet structure.
 * @return The result object, or NULL if absent or invalid.
 */
static cJSON *get_result(const struct rpc_wallet *wallet)
{
    cJSON *result;

    if (wallet == NULL || wallet->reply == NULL) {
        return NULL;
    }

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return NULL;
    }

    return result;
}

/**
 * Extracts the signature from a sign RPC response.
 *
 * @param wallet A pointer to the wallet structure.
 * @return A newly allocated signature, or NULL on failure.
 */
static char *get_signature(const struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *signature;

    result = get_result(wallet);

    if (result == NULL) {
        return NULL;
    }

    signature = cJSON_GetObjectItem(result, "signature");

    if (signature == NULL ||
        !cJSON_IsString(signature) ||
        signature->valuestring == NULL) {
        return NULL;
    }

    return duplicate_string(
        signature->valuestring,
        MAX_DATA_SIZE
    );
}

