/* main.c */

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

static const size_t command_count =
    sizeof(commands) / sizeof(commands[0]);

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

static void print_global_help(FILE *stream, const char *program)
{
    size_t i;

    fprintf(
        stream,
        "Usage:\n"
        "  %s TXID\n"
        "  echo TXID | %s\n"
        "  %s COMMAND [ARGUMENTS]\n"
        "  %s COMMAND help\n"
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
        "Examples:\n"
        "  %s init\n"
        "  %s TXID\n"
        "  echo TXID | %s\n"
        "  %s payment new\n"
        "  %s payment new --amount 650000\n"
        "  %s payment list\n"
        "  %s payment subaddr 1\n"
        "  %s payment PAYMENT_ID\n"
        "  echo PAYMENT_ID | %s payment\n"
        "  %s spend-proof TXID --signature SIGNATURE\n"
        "  echo TXID | %s spend-proof --signature SIGNATURE\n"
        "  %s tx-proof TXID --address ADDRESS --signature SIGNATURE\n"
        "  %s balance\n"
        "  %s bc-height\n"
        "\n"
        "Help:\n"
        "  %s help\n"
        "  %s payment help\n"
        "  %s spend-proof help\n",
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

static int dispatch_command(
    const struct command *command,
    const char *program,
    int argc,
    char **argv
)
{
    /* Help is intentionally available only one level below mnp. */
    if (argc == 3 && strcmp(argv[2], "help") == 0) {
        command->help(stdout, program);
        return EXIT_SUCCESS;
    }

    return command->handler(argc - 1, argv + 1);
}

static int dispatch_monitor(int argc, char **argv)
{
    /*
     * monitor_main() handles both:
     *
     *     mnp TXID
     *     echo TXID | mnp
     */
    return monitor_main(argc, argv);
}

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
        /* A bare interactive invocation must not block on stdin. */
        if (isatty(STDIN_FILENO)) {
            print_global_help(stdout, program);
            return EXIT_SUCCESS;
        }

        return dispatch_monitor(argc, argv);
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

    /*
     * A single non-command argument is treated as a TXID.
     * monitor_main() performs the actual TXID validation.
     */
    if (argc == 2) {
        return dispatch_monitor(argc, argv);
    }

    fprintf(
        stderr,
        "%s: unknown command '%s'\n"
        "Try '%s help' for usage.\n",
        program,
        argv[1],
        program
    );

    return EXIT_FAILURE;
}
