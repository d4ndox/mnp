# AGENTS.md

## Scope

These instructions apply to the entire repository.

Read `tests/README.MD` before performing integration or live Stagenet tests.

## General rules

* Work from the repository root unless a command explicitly requires another directory.
* Prefer the existing project structure and scripts over introducing new tooling.
* Do not modify production source code unless the user explicitly asks for a fix.
* Do not commit or push unless explicitly requested.
* Do not rewrite unrelated files.
* Keep changes minimal and focused.
* Preserve existing C style and project conventions.

## Build

Use a clean Debug build for tests:

```bash
rm -rf tests/.build

cmake \
    -S . \
    -B tests/.build \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build tests/.build --parallel
```

Use:

```bash
export MNP="$PWD/tests/.build/mnp"
export PATH="$PWD/tests/.build:$PATH"
```

Do not test an unrelated installed `mnp` binary.

## Offline tests

Always run the offline suite before live integration tests:

```bash
ctest \
    --test-dir tests/.build \
    --output-on-failure

python3 tests/offline_test.py

for file in tests/*.sh; do
    bash -n "$file" || exit 1
done
```

Report offline failures before continuing.

## Stagenet environment

The developer owns the external Stagenet environment.

Before live tests, the developer starts:

```bash
./tests/start_stagenet.sh
```

This provides:

```text
monerod
    127.0.0.1:38081

customerwallet
    full-key / spend-capable wallet
    127.0.0.1:30185

merchant wallet
    view-only wallet
    127.0.0.1:38084
```

The expected tmux session is:

```text
mnp-stagenet
```

with:

```text
0: monerod
1: customer
2: merchant
```

Codex MUST NOT start, stop, restart or kill these processes.

Codex MUST NOT run:

```text
./tests/start_stagenet.sh
./tests/stop_stagenet.sh
```

unless explicitly instructed by the user.

## Wallet roles

### customerwallet

The customer wallet is a full-key wallet.

It contains the private spend key and is used to send Stagenet transactions.

Default local test RPC:

```text
host:     127.0.0.1
port:     30185
user:     customer
password: customer-test
```

The RPC uses HTTP Digest authentication.

Set:

```bash
export RPC_HOST=127.0.0.1
export RPC_PORT=30185
export RPC_USER=customer
export RPC_PASSWORD=customer-test
```

Do not print the password in reports unless required to diagnose the local test
environment.

### merchant wallet

The merchant wallet is view-only.

It is used by `mnp` for merchant-side operations.

Expected RPC:

```text
127.0.0.1:38084
```

`mnp` reads its configuration through:

```text
~/.mnp.ini
```

which normally points to:

```text
~/.config/mnp/mnp.ini
```

For Stagenet tests the active configuration must use:

```text
host = 127.0.0.1
port = 38084
```

Do not modify the developer's wallet files.

Do not change the merchant RPC configuration unless explicitly requested.

## Live-test safety

All live tests use Stagenet.

Set:

```bash
export MNP_TEST_NETWORK=stagenet
```

Never submit transactions to mainnet.

Use only small atomic-unit amounts for transfer tests.

Do not run large load tests before normal integration tests pass.

Do not submit repeated transactions merely because Stagenet confirmation is
slow.

## External-service verification

Before live tests, verify:

```bash
tmux list-windows -t mnp-stagenet
pgrep -af monerod
pgrep -af monero-wallet-rpc
ss -ltnp | grep -E '38081|30185|38084'
```

Verify daemon health:

```bash
curl -s http://127.0.0.1:38081/get_height
```

Verify customer RPC using HTTP Digest authentication.

Verify merchant access through:

```bash
"$MNP" bc-height
"$MNP" balance
"$MNP" balance --unlocked
"$MNP" payment list
```

If an external prerequisite is unavailable:

* continue offline tests where possible
* mark dependent live tests as `SKIP`
* report the missing prerequisite
* do not attempt to replace or restart the service

## MariaDB

Codex may initialize and use the test database.

Verify:

```bash
mariadb -e "SELECT 1;"
```

Initialize:

```bash
mariadb < tests/create_db.sql
```

Do not delete unrelated or historical rows.

For assertions, isolate rows created by the current test run.

## Background helpers

Codex may start test helpers such as:

```text
tests/txdetectdb.sh
txid FIFO readers
temporary polling processes
```

Record every PID started by Codex.

Codex may terminate only processes it started.

Do not terminate the external Monero environment.

## FIFO behavior

Do not assume FIFOs broadcast data.

Only one reader receives bytes written to a FIFO.

Avoid competing readers.

Keep the `txid` FIFO drained during live transaction tests to prevent blocking
writer processes.

Use the configured mnp work directory rather than assuming a path when the
configuration provides one.

## Waiting and polling

Never wait indefinitely.

Blockchain confirmation, FIFO reads and database polling must use:

```text
timeout
```

or an explicit deadline loop.

A timeout is a test result, not permission to submit duplicate transactions.

## Error handling

For every failed test report:

```text
TEST:
COMMAND:
EXIT STATUS:
STDOUT:
STDERR:
EXPECTED:
```

Do not hide failed assertions.

Do not silently change source code to make tests pass.

If a likely production bug is found:

1. document the reproduction
2. identify the relevant source area if possible
3. report the failure
4. wait for an explicit request before changing production code

## Test result

At the end of a complete test run report:

```text
PASS:
FAIL:
SKIP:
```

Also include:

```text
branch
commit
build result
CTest result
offline regression result
monerod availability
customerwallet RPC availability
merchant RPC availability
MariaDB availability
number of live transactions submitted
```

Keep the final report concise and actionable.

## Primary integration-test documentation

The canonical test procedure is:

```text
tests/README.MD
```

When these instructions and the test README differ:

* `AGENTS.md` governs Codex behavior and safety
* `tests/README.MD` governs the concrete test procedure

Report material inconsistencies instead of guessing.

