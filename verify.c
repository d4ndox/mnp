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

#include "verify.h"

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

struct verify_args {
    const char *address;
    const char *signature;
    const char *message_argument;
};

static char *duplicate_string(const char *value, size_t maximum);
static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *get_config_path(void);
static void free_config(struct Config *config);
static int parse_arguments(int argc, char **argv, struct verify_args *args);
static char *read_stdin(void);
static char *get_message(const struct verify_args *args);
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config);
static void free_wallet(struct rpc_wallet *wallet);
static int signature_is_good(const struct rpc_wallet *wallet);

/**
 * Verifies a Monero message signature.
 *
 * The signed data may be supplied as a positional argument or through standard input.
 * Exactly one trailing LF is removed from standard input.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS for a valid signature, or EXIT_FAILURE otherwise.
 */
int verify_main(int argc, char **argv)
{
    struct verify_args args = {0};
    struct Config config = {0};
    struct rpc_wallet wallet = {0};
    char *config_path = NULL;
    char *message = NULL;
    int good;
    int result = EXIT_FAILURE;

    if (parse_arguments(argc, argv, &args) == -1) {
        fprintf(
            stderr,
            "Try 'mnp verify help' for usage.\n"
        );
        return EXIT_FAILURE;
    }

    if (val_address(args.address) < 0) {
        fprintf(stderr, "mnp verify: invalid address\n");
        return EXIT_FAILURE;
    }

    if (val_signature(args.signature) < 0) {
        fprintf(stderr, "mnp verify: invalid signature\n");
        return EXIT_FAILURE;
    }

    message = get_message(&args);

    if (message == NULL) {
        fprintf(stderr, "mnp verify: message is required\n");
        goto done;
    }

    config_path = get_config_path();

    if (config_path == NULL) {
        fprintf(stderr, "mnp verify: cannot determine configuration path\n");
        goto done;
    }

    if (ini_parse(config_path, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp verify: cannot load configuration '%s'\n",
            config_path
        );
        goto done;
    }

    if (init_wallet(&wallet, &config) == -1) {
        fprintf(stderr, "mnp verify: incomplete RPC configuration\n");
        goto done;
    }

    wallet.data = duplicate_string(message, MAX_DATA_SIZE);
    wallet.saddr = duplicate_string(args.address, MAX_DATA_SIZE);
    wallet.signature = duplicate_string(args.signature, MAX_DATA_SIZE);

    if (wallet.data == NULL ||
        wallet.saddr == NULL ||
        wallet.signature == NULL) {
        fprintf(stderr, "mnp verify: invalid input\n");
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp verify: could not connect to host %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    good = signature_is_good(&wallet);

    if (good == -1) {
        fprintf(stderr, "mnp verify: invalid wallet RPC response\n");
        goto done;
    }

    fprintf(stdout, "%s\n", good ? "true" : "false");

    if (good) {
        result = EXIT_SUCCESS;
    }

done:
    free(message);
    free(config_path);
    free_wallet(&wallet);
    free_config(&config);

    return result;
}

/**
 * Prints usage information for the verify command.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in usage examples.
 */
void verify_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s verify ADDRESS SIGNATURE MESSAGE\n"
        "  echo MESSAGE | %s verify ADDRESS SIGNATURE\n"
        "\n"
        "Verify a Monero message signature.\n"
        "\n"
        "Output:\n"
        "  true   The signature is valid.\n"
        "  false  The signature is invalid.\n"
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
 * Parses RPC configuration values.
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
}

/**
 * Parses verify command arguments.
 *
 * @param argc The number of arguments.
 * @param argv The argument vector.
 * @param args A pointer receiving parsed arguments.
 * @return 0 on success, or -1 on invalid arguments.
 */
static int parse_arguments(int argc, char **argv, struct verify_args *args)
{
    if (args == NULL) {
        return -1;
    }

    if (argc >= 2 &&
        (strcmp(argv[1], "help") == 0 ||
         strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "-h") == 0)) {
        verify_help(stdout, argv[0]);
        exit(EXIT_SUCCESS);
    }

    if (argc < 3 || argc > 4) {
        return -1;
    }

    args->address = argv[1];
    args->signature = argv[2];
    args->message_argument = argc == 4 ? argv[3] : NULL;

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
 * Obtains the signed message from an argument or stdin.
 *
 * @param args Parsed verify arguments.
 * @return A newly allocated message, or NULL on failure.
 */
static char *get_message(const struct verify_args *args)
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
 * Initializes a wallet RPC request for signature verification.
 *
 * @param wallet A pointer to the wallet structure.
 * @param config A pointer to the loaded configuration.
 * @return 0 on success, or -1 on failure.
 */
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config)
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

    wallet->monero_rpc_method = VERIFY_MESSAGE;
    wallet->host = duplicate_string(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = duplicate_string(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = duplicate_string(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = duplicate_string(config->rpc_password, MAX_DATA_SIZE);

    if (wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL) {
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
    free(wallet->data);
    free(wallet->saddr);
    free(wallet->signature);

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

/**
 * Extracts the signature verification state.
 *
 * @param wallet A pointer to the wallet structure.
 * @return 1 if valid, 0 if invalid, or -1 on malformed response.
 */
static int signature_is_good(const struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *good;

    if (wallet == NULL || wallet->reply == NULL) {
        return -1;
    }

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return -1;
    }

    good = cJSON_GetObjectItem(result, "good");

    if (good == NULL || !cJSON_IsBool(good)) {
        return -1;
    }

    return cJSON_IsTrue(good) ? 1 : 0;
}

