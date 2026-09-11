/*
 * main.c
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "balance.h"
#include "bc_height.h"
#include "cleanup.h"
#include "globaldefs.h"
#include "init.h"
#include "monitor.h"
#include "payment.h"
#include "sign.h"
#include "spend_proof.h"
#include "transactions.h"
#include "transfer.h"
#include "tx_proof.h"
#include "verify.h"

struct command {
    const char *name;
    int (*handler)(int argc, char **argv);
    void (*help)(FILE *stream, const char *program);
};

static int version_main(int argc, char **argv);
static void version_help(FILE *stream, const char *program);
static const struct command *find_command(const char *name);
static void print_global_help(FILE *stream, const char *program);
static int parse_global_options(int *argc, char ***argv);
static int dispatch_command(const struct command *command, const char *program,
                            int argc, char **argv);

static const struct command commands[] = {
    {"init", init_main, init_help},
    {"cleanup", cleanup_main, cleanup_help},
    {"payment", payment_main, payment_help},
    {"spend-proof", spend_proof_main, spend_proof_help},
    {"tx-proof", tx_proof_main, tx_proof_help},
    {"sign", sign_main, sign_help},
    {"verify", verify_main, verify_help},
    {"transactions", transactions_main, transactions_help},
    {"transfer", transfer_main, transfer_help},
    {"balance", balance_main, balance_help},
    {"bc-height", bc_height_main, bc_height_help},
    {"version", version_main, version_help},
    {NULL, NULL, NULL}
};

/**
 * Prints the application version.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE if arguments are invalid.
 */
static int version_main(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "%s version: unexpected argument\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("%s %s\n", argv[0], VERSION);

    return EXIT_SUCCESS;
}

/**
 * Prints help for the version command.
 *
 * @param stream The output stream receiving the help text.
 * @param program The executable name used in the usage text.
 */
static void version_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s version\n"
        "  %s --version\n"
        "\n"
        "Print the mnp version.\n",
        program,
        program
    );
}

/**
 * Finds a registered top-level command.
 *
 * @param name The command name to find.
 * @return A pointer to the command definition, or NULL if no command matches.
 */
static const struct command *find_command(const char *name)
{
    size_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }

    return NULL;
}

/**
 * Prints global command-line help.
 *
 * @param stream The output stream receiving the help text.
 * @param program The executable name used in usage examples.
 */
static void print_global_help(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s [--config FILE] TXID [--notify-at N] [--confirmation N]\n"
        "  echo TXID | %s [--config FILE] [--notify-at N] [--confirmation N]\n"
        "  %s [--config FILE] COMMAND [ARGUMENTS]\n"
        "  %s [--config FILE] COMMAND help\n"
        "  %s version\n"
        "  %s --version\n"
        "\n"
        "Global options:\n"
        "  --config FILE\n"
        "      Use FILE instead of the default ~/.mnp.ini configuration.\n"
        "\n"
        "Transaction monitor:\n"
        "  TXID\n"
        "      Monitor a transaction. The TXID may be supplied as an\n"
        "      argument or through stdin.\n"
        "\n"
        "  --notify-at N\n"
        "      Select when the transaction should trigger notification:\n"
        "        0  none\n"
        "        1  txpool\n"
        "        2  confirmed\n"
        "        3  unlocked\n"
        "\n"
        "  --confirmation N\n"
        "      Required confirmation count when --notify-at 2 is used.\n"
        "\n"
        "Commands:\n"
        "  init           Initialize the mnp working directory\n"
        "  cleanup        Remove the mnp working directory\n"
        "  payment        Create and inspect payments\n"
        "  spend-proof    Verify a spend proof\n"
        "  tx-proof       Verify a transaction proof\n"
        "  sign           Sign data with the wallet view key\n"
        "  verify         Verify a message signature\n"
        "  transactions   List wallet transactions\n"
        "  transfer       Send Monero to one or more destinations\n"
        "  balance        Print the wallet balance\n"
        "  bc-height      Print the blockchain height\n"
        "  version        Print the mnp version\n"
        "\n"
        "Help:\n"
        "  %s help\n"
        "  %s init help\n"
        "  %s cleanup help\n"
        "  %s payment help\n"
        "  %s spend-proof help\n"
        "  %s tx-proof help\n"
        "  %s sign help\n"
        "  %s verify help\n"
        "  %s transactions help\n"
        "  %s transfer help\n"
        "  %s balance help\n"
        "  %s bc-height help\n"
        "\n"
        "Examples:\n"
        "  %s version\n"
        "  %s --version\n"
        "  %s --config /tmp/.mnp.ini balance\n"
        "  %s --config /tmp/.mnp.ini transfer "
        "'monero:ADDRESS?tx_amount=0.000000055555'\n"
        "  %s TXID\n"
        "  %s TXID --notify-at 1\n"
        "  %s TXID --notify-at 2 --confirmation 3\n"
        "  echo TXID | %s\n"
        "  echo TXID | %s --notify-at 2 --confirmation 3\n",
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
        program,
        program,
        program
    );
}

/**
 * Parses global command-line options and removes them from argv.
 *
 * The selected configuration path is exported through MNP_CONFIG so command
 * modules can use one shared configuration-selection mechanism.
 *
 * @param argc A pointer to the argument count.
 * @param argv A pointer to the argument vector.
 * @return 0 on success, or -1 if a global option is invalid.
 */
static int parse_global_options(int *argc, char ***argv)
{
    char **arguments;
    const char *config_path;
    int i;

    if (argc == NULL || argv == NULL || *argv == NULL) {
        return -1;
    }

    arguments = *argv;

    if (*argc < 2 || strcmp(arguments[1], "--config") != 0) {
        return 0;
    }

    if (*argc < 3) {
        fprintf(stderr, "%s: --config requires a file\n", arguments[0]);
        return -1;
    }

    config_path = arguments[2];

    if (config_path[0] == '\0') {
        fprintf(stderr, "%s: --config requires a file\n", arguments[0]);
        return -1;
    }

    if (setenv("MNP_CONFIG", config_path, 1) != 0) {
        perror("mnp: cannot set configuration path");
        return -1;
    }

    for (i = 1; i + 2 < *argc; i++) {
        arguments[i] = arguments[i + 2];
    }

    *argc -= 2;
    arguments[*argc] = NULL;

    return 0;
}

/**
 * Dispatches a top-level command while preserving the executable name as
 * argv[0] for the command handler.
 *
 * @param command The command definition to dispatch.
 * @param program The executable name.
 * @param argc The original argument count.
 * @param argv The original argument vector.
 * @return The command handler's exit status.
 */
static int dispatch_command(const struct command *command, const char *program,
                            int argc, char **argv)
{
    char **command_argv;
    int command_argc;
    int status;
    int i;

    if (command == NULL || program == NULL || argc < 2 || argv == NULL) {
        return EXIT_FAILURE;
    }

    if (argc == 3 &&
        (strcmp(argv[2], "help") == 0 ||
         strcmp(argv[2], "--help") == 0 ||
         strcmp(argv[2], "-h") == 0)) {
        command->help(stdout, program);
        return EXIT_SUCCESS;
    }

    command_argc = argc - 1;

    command_argv = calloc((size_t)command_argc + 1, sizeof(*command_argv));

    if (command_argv == NULL) {
        fprintf(stderr, "%s: out of memory\n", program);
        return EXIT_FAILURE;
    }

    command_argv[0] = argv[0];

    for (i = 1; i < command_argc; i++) {
        command_argv[i] = argv[i + 1];
    }

    command_argv[command_argc] = NULL;

    status = command->handler(command_argc, command_argv);

    free(command_argv);

    return status;
}

/**
 * Main application entry point.
 *
 * Parses global options, dispatches known commands, and forwards all remaining
 * invocations to the transaction monitor.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or EXIT_FAILURE on error.
 */
int main(int argc, char **argv)
{
    const struct command *command;
    const char *program;

    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        fprintf(stderr, "mnp: invalid process arguments\n");
        return EXIT_FAILURE;
    }

    program = argv[0];

    if (parse_global_options(&argc, &argv) == -1) {
        return EXIT_FAILURE;
    }

    if (argc == 1) {
        if (isatty(STDIN_FILENO)) {
            print_global_help(stdout, program);
            return EXIT_SUCCESS;
        }

        return monitor_main(argc, argv);
    }

    if (strcmp(argv[1], "help") == 0 ||
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        if (argc != 2) {
            fprintf(
                stderr,
                "%s: help does not accept additional arguments\n",
                program
            );
            return EXIT_FAILURE;
        }

        print_global_help(stdout, program);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "--version") == 0) {
        if (argc != 2) {
            fprintf(
                stderr,
                "%s: --version does not accept additional arguments\n",
                program
            );
            return EXIT_FAILURE;
        }

        printf("%s %s\n", program, VERSION);
        return EXIT_SUCCESS;
    }

    command = find_command(argv[1]);

    if (command != NULL) {
        return dispatch_command(
            command,
            program,
            argc,
            argv
        );
    }

    return monitor_main(argc, argv);
}
