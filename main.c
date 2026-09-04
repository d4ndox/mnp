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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "balance.h"
#include "bc_height.h"
#include "cleanup.h"
#include "init.h"
#include "monitor.h"
#include "payment.h"
#include "spend_proof.h"
#include "tx_proof.h"

typedef int (*command_handler_fn)(int argc, char **argv);
typedef void (*command_help_fn)(FILE *stream, const char *program);

struct command {
    const char *name;
    const char *description;
    command_handler_fn handler;
    command_help_fn help;
};

static const struct command commands[] = {
    {
        "init",
        "Initialize the mnp working directory",
        init_main,
        init_help,
    },
    {
        "cleanup",
        "Remove the mnp working directory",
        cleanup_main,
        cleanup_help,
    },
    {
        "payment",
        "Create and inspect payments",
        payment_main,
        payment_help,
    },
    {
        "spend-proof",
        "Verify a spend proof",
        spend_proof_main,
        spend_proof_help,
    },
    {
        "tx-proof",
        "Verify a transaction proof",
        tx_proof_main,
        tx_proof_help,
    },
    {
        "balance",
        "Print the wallet balance",
        balance_main,
        balance_help,
    },
    {
        "bc-height",
        "Print the blockchain height",
        bc_height_main,
        bc_height_help,
    },
};

static const size_t command_count = sizeof(commands) / sizeof(commands[0]);

static const struct command *find_command(const char *name);
static void print_global_help(FILE *stream, const char *program);
static int dispatch_command(const struct command *command, const char *program, int argc, char **argv);

/**
 * Dispatches mnp commands and transaction-monitor invocations.
 *
 * Known top-level commands are forwarded to their command handlers. Any
 * argument sequence that does not begin with a known command is handled by
 * the transaction monitor, allowing both positional and piped transaction IDs.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line argument vector.
 * @return EXIT_SUCCESS on success, or a command-specific failure status.
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

    command = find_command(argv[1]);

    if (command != NULL) {
        return dispatch_command(command, program, argc, argv);
    }

    return monitor_main(argc, argv);
}

/**
 * Finds a registered top-level command by name.
 *
 * @param name The command name to search for.
 * @return A pointer to the matching command structure, or NULL if no command
 *         matches.
 */
static const struct command *find_command(const char *name)
{
    size_t i;

    for (i = 0; i < command_count; ++i) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }

    return NULL;
}

/**
 * Prints global usage information for the mnp command-line interface.
 *
 * @param stream The output stream receiving the help text.
 * @param program The program name used in usage examples.
 */
static void print_global_help(FILE *stream, const char *program)
{
    size_t i;

    fprintf(
        stream,
        "Usage:\n"
        "  %s TXID [--notify-at N] [--confirmation N]\n"
        "  echo TXID | %s [--notify-at N] [--confirmation N]\n"
        "  %s COMMAND [ARGUMENTS]\n"
        "  %s COMMAND help\n"
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
        "Commands:\n",
        program,
        program,
        program,
        program
    );

    for (i = 0; i < command_count; ++i) {
        fprintf(
            stream,
            "  %-14s %s\n",
            commands[i].name,
            commands[i].description
        );
    }

    fprintf(
        stream,
        "\n"
        "Help:\n"
        "  %s help\n"
        "  %s init help\n"
        "  %s cleanup help\n"
        "  %s payment help\n"
        "  %s spend-proof help\n"
        "  %s tx-proof help\n"
        "  %s balance help\n"
        "  %s bc-height help\n"
        "\n"
        "Examples:\n"
        "  %s TXID\n"
        "  %s TXID --notify-at 1\n"
        "  %s TXID --notify-at 2 --confirmation 3\n"
        "  echo TXID | %s\n"
        "  echo TXID | %s --notify-at 2 --confirmation 3\n"
        "\n"
        "  %s init\n"
        "  %s payment new\n"
        "  %s payment new --amount 650000\n"
        "  %s payment list\n"
        "  %s payment subaddr 1\n"
        "  %s payment PAYMENT_ID\n"
        "  echo PAYMENT_ID | %s payment\n"
        "\n"
        "  %s spend-proof TXID --signature SIGNATURE\n"
        "  echo TXID | %s spend-proof --signature SIGNATURE\n"
        "\n"
        "  %s tx-proof TXID --address ADDRESS --signature SIGNATURE\n"
        "  echo TXID | %s tx-proof --address ADDRESS "
        "--signature SIGNATURE\n"
        "\n"
        "  %s balance\n"
        "  %s bc-height\n",
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
 * Dispatches a registered top-level command.
 *
 * A direct "help" argument is handled without invoking the command itself.
 * All other arguments are forwarded to the command handler with the top-level
 * command removed from the argument vector.
 *
 * @param command A pointer to the command structure to dispatch.
 * @param program The program name used by the help handler.
 * @param argc The original number of command-line arguments.
 * @param argv The original command-line argument vector.
 * @return EXIT_SUCCESS for help output, or the return value of the command
 *         handler.
 */
static int dispatch_command(const struct command *command, const char *program, int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[2], "help") == 0) {
        command->help(stdout, program);
        return EXIT_SUCCESS;
    }

    return command->handler(argc - 1, argv + 1);
}
