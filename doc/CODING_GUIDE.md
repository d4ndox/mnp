# Coding Guide for Monero Named Pipes

Language: GNU C, C99.

Build system: CMake + Make.

Libraries:

- `libcurl` (linked)
- `inih` (`inih/`)
- `cJSON` (`cjson/`)

## Build

Debug:

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

Release:

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Install:

```bash
sudo cmake --install .
```

## General Architecture

Monero Named Pipes communicates with `monero-wallet-rpc` through its JSON-RPC interface.

The current project builds a single executable:

```text
mnp
```

The former standalone executables `mnpd` and `mnp-payment` are no longer part of the command-line design. Their functionality is provided through subcommands of `mnp`.

Main command forms:

```text
mnp TXID [--notify-at N] [--confirmation N]
echo TXID | mnp [--notify-at N] [--confirmation N]

mnp init
mnp cleanup
mnp payment ...
mnp spend-proof ...
mnp tx-proof ...
mnp balance
mnp bc-height
mnp version
```

`main.c` is responsible only for top-level command dispatch. Command-specific logic belongs in the respective module.

## File Structure

```text
.
├── main.c
├── init.c
├── init.h
├── cleanup.c
├── cleanup.h
├── monitor.c
├── monitor.h
├── payment.c
├── payment.h
├── spend_proof.c
├── spend_proof.h
├── tx_proof.c
├── tx_proof.h
├── balance.c
├── balance.h
├── bc_height.c
├── bc_height.h
├── rpc_call.c
├── rpc_call.h
├── wallet.c
├── wallet.h
├── validate.c
├── validate.h
├── delquotes.c
├── delquotes.h
├── globaldefs.h
├── inih/
│   ├── ini.c
│   └── ini.h
├── cjson/
│   ├── cJSON.c
│   └── cJSON.h
├── doc/
├── example/
├── tests/
└── CMakeLists.txt
```

### `main.c`

Top-level command dispatcher.

Responsibilities:

- global help
- version output
- dispatching known subcommands
- forwarding all non-subcommand invocations to the transaction monitor

### `monitor.c`

Transaction monitoring.

Responsibilities:

- read TXID from argv or stdin
- parse `--notify-at`
- parse `--confirmation`
- poll Monero wallet RPC
- detect confirmation / unlocked state
- create transaction named pipes
- emit transaction notifications

### `payment.c`

Payment-related commands.

Supported forms:

```text
mnp payment new [--amount AMOUNT]
mnp payment list
mnp payment subaddr INDEX [--amount AMOUNT]
mnp payment PAYMENT_ID [--amount AMOUNT]
echo PAYMENT_ID | mnp payment [--amount AMOUNT]
```

### `spend_proof.c`

Spend-proof verification.

```text
mnp spend-proof TXID --signature SIGNATURE [--message MESSAGE]
echo TXID | mnp spend-proof --signature SIGNATURE [--message MESSAGE]
```

### `tx_proof.c`

Transaction-proof verification.

```text
mnp tx-proof TXID --address ADDRESS --signature SIGNATURE [--message MESSAGE]
echo TXID | mnp tx-proof --address ADDRESS --signature SIGNATURE [--message MESSAGE]
```

### `balance.c`

Queries the wallet balance once and writes the raw balance to stdout.

```text
mnp balance
```

### `bc_height.c`

Queries the current blockchain height once and writes the raw height to stdout.

```text
mnp bc-height
```

### `init.c`

Creates the configured mnp working directory, transaction directory, state file, and named pipes.

### `cleanup.c`

Removes the configured mnp working directory recursively.

### `rpc_call.c`

Builds the JSON-RPC request according to `enum monero_rpc_method` and calls the wallet transport layer.

### `wallet.c`

Communicates with `monero-wallet-rpc` through libcurl.

### `globaldefs.h`

Contains project-wide constants, macros, paths, defaults, and the application version.

## Data Structure

The primary RPC structure is `struct rpc_wallet`, defined in `rpc_call.h`.

It contains:

- the selected RPC method
- RPC connection parameters
- command-specific request values
- the parsed cJSON reply

Typical fields include:

```c
struct rpc_wallet {
    int monero_rpc_method;
    char *params;
    char *account;
    char *host;
    char *port;
    char *user;
    char *pwd;
    char *balance;
    char *height;
    char *file;
    char *txid;
    char *payid;
    char *saddr;
    char *iaddr;
    char *amount;
    char *conf;
    char *locked;
    char *fifo;
    char *message;
    char *signature;
    char *proof;
    int idx;
    cJSON *reply;
};
```

The exact structure in `rpc_call.h` is authoritative.

`enum monero_rpc_method` defines the available wallet RPC operations. Examples include:

```c
GET_HEIGHT
GET_BALANCE
GET_TXID
GET_LIST
GET_SUBADDR
NEW_SUBADDR
MK_IADDR
MK_URI
CHECK_SPEND_PROOF
CHECK_TX_PROOF
```

## Command Module Layout

New command modules should follow this layout:

```c
#include "module.h"

/* includes */

struct command_args {
    ...
};

static int helper_one(...);
static void helper_two(...);

int command_main(int argc, char **argv)
{
    ...
}

/**
 * Describes what the function does.
 *
 * @param value Description of the parameter.
 * @return Description of the return value.
 */
static int helper_one(...)
{
    ...
}
```

Rules:

- Keep private function prototypes above the command entry point.
- Do not add documentation comments to prototypes.
- Put the command entry point before helper implementations.
- Prefer compact function signatures such as:

```c
int monitor_main(int argc, char **argv)
```

- Break long signatures only when required to stay readable and within the line-length limit.
- Keep full function implementations; do not hide logic behind undocumented macros.

## Documentation Comments

Use Doxygen-style comments directly above function implementations:

```c
/**
 * Extracts the signature verification status from the Monero wallet RPC response.
 *
 * @param wallet A pointer to the rpc_wallet structure containing the RPC response.
 * @return 1 if the proof is valid, 0 if the proof is invalid, or -1 if extraction fails.
 */
static int proof_is_good(const struct rpc_wallet *wallet)
{
    ...
}
```

Do not document prototypes.

Short non-function comments may use:

```c
/* Important reason for this behavior. */
```

Avoid `//` comments.

Comments should explain why code exists or describe API behavior. Avoid comments that merely repeat the code.

## Error Handling and Output

General rules:

- errors go to `stderr`
- command output goes to `stdout`
- named-pipe notifications go to the appropriate FIFO
- return `EXIT_SUCCESS` on success
- return `EXIT_FAILURE` on command failure

Commands intended for scripting should not add labels to stdout.

Examples:

```text
mnp balance
mnp bc-height
mnp spend-proof ...
mnp tx-proof ...
```

`balance` and `bc-height` print only their raw values.

Proof commands print:

```text
true
```

or:

```text
false
```

## Syslog

Use syslog where it provides operational value.

Suggested levels:

- `LOG_ERR` for operational errors
- `LOG_INFO` for verbose runtime information
- `LOG_DEBUG` for debug-only information

Use a unique identifier when calling `openlog()`.

Example:

```c
openlog("mnp:", LOG_PID, LOG_USER);
```

## Style Guide

- GNU C / C99
- 4 spaces per indentation level
- no tabs
- maximum line length: 120 characters
- prefer simple, readable control flow
- keep functions focused
- free all dynamically allocated memory
- validate external input before use
- print meaningful error messages
- avoid duplicated logic where a small shared helper makes the code clearer
- prefer `static` for functions that are private to a translation unit

## Headers

Each command module should expose only its public command entry points.

Example:

```c
#ifndef MNP_BALANCE_H
#define MNP_BALANCE_H

#include <stdio.h>

int balance_main(int argc, char **argv);
void balance_help(FILE *stream, const char *program);

#endif
```

Private helpers stay in the corresponding `.c` file.

## Version

The application version is defined in `globaldefs.h`.

Both forms should report the same version:

```bash
mnp version
mnp --version
```

The version string should be read from the existing `VERSION` macro rather than duplicated elsewhere.
