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

#include "bc_height.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "globaldefs.h"
#include "inih/ini.h"
#include "rpc_call.h"

static int config_handler(void *user, const char *section, const char *name, const char *value);
static char *config_path(void);
static int init_wallet(struct rpc_wallet *wallet, const struct Config *config);
static int print_height(const struct rpc_wallet *wallet);
static void free_wallet(struct rpc_wallet *wallet);
static void free_config(struct Config *config);

/**
 * Retrieves and prints the current Monero blockchain height.
 *
 * The blockchain height is written to standard output without additional
 * labels or formatting.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE if the blockchain height
 *         cannot be retrieved.
 */
int bc_height_main(int argc, char **argv)
{
    struct Config config = {0};
    struct rpc_wallet wallet = {0};
    char *ini = NULL;
    int result = EXIT_FAILURE;

    if (argc != 1) {
        fprintf(
            stderr,
            "mnp bc-height: unexpected argument '%s'\n"
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
            "mnp bc-height: cannot determine config path\n"
        );
        goto done;
    }

    if (ini_parse(ini, config_handler, &config) < 0) {
        fprintf(
            stderr,
            "mnp bc-height: cannot load configuration '%s'\n",
            ini
        );
        goto done;
    }

    if (init_wallet(&wallet, &config) == -1) {
        fprintf(
            stderr,
            "mnp bc-height: incomplete RPC configuration\n"
        );
        goto done;
    }

    if (rpc_call(&wallet) < 0) {
        fprintf(
            stderr,
            "mnp bc-height: could not connect to host %s:%s\n",
            wallet.host,
            wallet.port
        );
        goto done;
    }

    if (print_height(&wallet) == -1) {
        fprintf(
            stderr,
            "mnp bc-height: invalid wallet RPC response\n"
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

/**
 * Prints usage information for the bc-height command.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in the usage example.
 */
void bc_height_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s bc-height\n"
        "\n"
        "Print the current blockchain height.\n"
        "\n"
        "Output:\n"
        "  The raw blockchain height is written to stdout.\n",
        program
    );
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
    } else {
        return 0;
    }

#undef MATCH

    return 1;
}

/**
 * Builds the path to the mnp configuration file.
 *
 * @return A dynamically allocated string containing the configuration path,
 *         or NULL if the path cannot be created.
 */
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

/**
 * Initializes a Monero wallet RPC request for retrieving the blockchain height.
 *
 * @param wallet A pointer to the rpc_wallet structure to initialize.
 * @param config A pointer to the loaded mnp configuration.
 * @return 0 on success, or -1 if initialization fails.
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

    wallet->monero_rpc_method = GET_HEIGHT;
    wallet->host = strndup(config->rpc_host, MAX_DATA_SIZE);
    wallet->port = strndup(config->rpc_port, MAX_DATA_SIZE);
    wallet->user = strndup(config->rpc_user, MAX_DATA_SIZE);
    wallet->pwd = strndup(config->rpc_password, MAX_DATA_SIZE);

    if (wallet->host == NULL ||
        wallet->port == NULL ||
        wallet->user == NULL ||
        wallet->pwd == NULL) {
        return -1;
    }

    return 0;
}

/**
 * Extracts and prints the blockchain height from the Monero wallet RPC response.
 *
 * @param wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return 0 on success, or -1 if the blockchain height cannot be extracted or printed.
 */
static int print_height(const struct rpc_wallet *wallet)
{
    cJSON *result;
    cJSON *height;
    char *value;

    if (wallet == NULL || wallet->reply == NULL) {
        return -1;
    }

    result = cJSON_GetObjectItem(wallet->reply, "result");

    if (result == NULL || !cJSON_IsObject(result)) {
        return -1;
    }

    height = cJSON_GetObjectItem(result, "height");

    if (height == NULL || !cJSON_IsNumber(height)) {
        return -1;
    }

    value = cJSON_PrintUnformatted(height);

    if (value == NULL) {
        return -1;
    }

    fprintf(stdout, "%s\n", value);
    free(value);

    return 0;
}

/**
 * Releases dynamically allocated fields in an rpc_wallet structure.
 *
 * @param wallet A pointer to the rpc_wallet structure to clean up.
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

    if (wallet->reply != NULL) {
        cJSON_Delete(wallet->reply);
    }
}

/**
 * Releases dynamically allocated fields in an mnp configuration structure.
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
}
