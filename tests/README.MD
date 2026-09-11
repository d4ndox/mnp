# Testing Monero Named Pipes

This document is the integration-test runbook for developers and Codex.

Codex should execute the test workflow from top to bottom and report failures
with the command, exit status, stdout and stderr.

## Test architecture

The Stagenet environment uses three Monero processes:

```text
monerod
    Stagenet daemon
    127.0.0.1:38081

customerwallet
    Full-key / spend-capable wallet
    Used to send test transactions
    Wallet RPC: 127.0.0.1:30185

merchant wallet
    View-only wallet
    Used by mnp for merchant-side operations
    Wallet RPC: 127.0.0.1:38084
```

The developer starts and stops these processes with:

```bash
./tests/start_stagenet.sh
./tests/stop_stagenet.sh
```

`start_stagenet.sh` uses a `tmux` session named:

```text
mnp-stagenet
```

with these windows:

```text
0: monerod
1: customer
2: merchant
```

The developer selects:

* one full-key wallet for `customerwallet`
* one view-only wallet for the merchant

The two wallets must be different.

The test wallets are expected to be Stagenet wallets.

## Runtime configuration

`start_stagenet.sh` creates runtime-only configuration files under:

```text
tests/.runtime/
```

The important files are:

```text
tests/.runtime/customer.mnp.ini
tests/.runtime/merchant-wallet-rpc.conf
```

`customer.mnp.ini` is used by `mnp --config` for spend-capable customer
operations.

`merchant-wallet-rpc.conf` is used to start the merchant wallet RPC and points
`tx-notify` at the freshly built test binary.

These files are generated for the local test environment and must not replace
the developer's normal configuration files.

## Wallet roles

### customerwallet

`customerwallet` is a full-key wallet.

It contains the private spend key and is used only for sending Stagenet
transactions during integration tests.

Its wallet RPC listens on:

```text
127.0.0.1:30185
```

The default local test credentials are:

```text
user:     customer
password: customer-test
```

The RPC uses HTTP Digest authentication.

Test scripts use:

```bash
export RPC_HOST=127.0.0.1
export RPC_PORT=30185
export RPC_USER=customer
export RPC_PASSWORD=customer-test
```

Direct RPC requests must use Digest authentication:

```bash
curl --digest -s \
    -u "$RPC_USER:$RPC_PASSWORD" \
    -H 'Content-Type: application/json' \
    -d '{
        "jsonrpc":"2.0",
        "id":"0",
        "method":"get_balance",
        "params":{"account_index":0}
    }' \
    "http://$RPC_HOST:$RPC_PORT/json_rpc"
```

For `mnp` commands that must spend funds, use:

```text
tests/.runtime/customer.mnp.ini
```

through:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    COMMAND
```

Do not use the default merchant configuration for successful transfer tests.

### Merchant wallet

The merchant wallet is view-only.

It does not contain the private spend key.

It is used by `mnp` for:

```text
payment URI generation
subaddress handling
balance queries
transaction history
transaction monitoring
view-key message signing
incoming payment detection
```

Its wallet RPC listens on:

```text
127.0.0.1:38084
```

The base merchant RPC configuration is:

```text
~/.config/mnp/monero-wallet-rpc.conf
```

For integration testing, `start_stagenet.sh` creates:

```text
tests/.runtime/merchant-wallet-rpc.conf
```

The runtime configuration must contain:

```text
tx-notify=/absolute/path/to/tests/.build/mnp --notify-at 1 %s
```

The developer's original merchant RPC configuration must not be modified by
Codex.

`mnp` normally reads:

```text
~/.mnp.ini
```

which normally points to:

```text
~/.config/mnp/mnp.ini
```

For the Stagenet integration environment the active merchant mnp configuration
must use:

```text
host = 127.0.0.1
port = 38084
```

Do not use the mainnet wallet RPC port `18084` during Stagenet tests.

## Codex execution contract

Before Codex begins integration testing, the developer starts the Monero
environment:

```bash
./tests/start_stagenet.sh
```

Codex MUST NOT start, stop, restart, kill or modify:

```text
monerod
customerwallet monero-wallet-rpc
merchant monero-wallet-rpc
```

Codex MUST NOT run:

```text
./tests/start_stagenet.sh
./tests/stop_stagenet.sh
```

unless explicitly instructed by the developer.

Codex is responsible for everything else where possible, including:

```text
repository inspection
clean build
CTest
offline regression tests
shell syntax checks
MariaDB schema setup
mnp init
starting txdetectdb.sh
draining notification FIFOs
creating payment requests
sending Stagenet transactions
CLI tests
transaction assertions
monitor tests
proof tests when matching fixtures exist
load tests
cleanup of processes started by Codex
```

For every successful `mnp transfer` test, use:

```text
--config tests/.runtime/customer.mnp.ini
```

Never submit transfers through the default merchant configuration.

Codex MUST NOT:

```text
use mainnet
modify or delete wallet files
print RPC passwords in reports
kill external Monero processes
modify the developer's ~/.mnp.ini
modify external wallet RPC configuration
commit changes unless explicitly requested
push changes unless explicitly requested
wait indefinitely
```

Every wait for blockchain state, FIFO data or transaction confirmation must use
a timeout or deadline.

If an external prerequisite is unavailable, Codex should report the problem
rather than attempting to replace or reconfigure it.

## Start the Stagenet environment

From the repository root:

```bash
./tests/start_stagenet.sh
```

Select:

```text
customer wallet:
    full-key / spend-capable wallet

merchant wallet:
    view-only wallet
```

Example:

```text
Select customer wallet (FULL KEY / spend-capable):
  1) customerwallet/customerwallet-stage
  ...
> 1

Select merchant wallet (VIEW-ONLY):
  ...
  6) myviewwallet-stage/mywalletviewkey-stagenet
> 6
```

The script must successfully start:

```text
127.0.0.1:38081  monerod
127.0.0.1:30185  customer wallet RPC
127.0.0.1:38084  merchant wallet RPC
```

It must also create:

```text
tests/.runtime/customer.mnp.ini
tests/.runtime/merchant-wallet-rpc.conf
```

Inspect the session when needed:

```bash
tmux list-windows -t mnp-stagenet
```

Attach:

```bash
tmux attach -t mnp-stagenet
```

Inside tmux:

```text
Ctrl-b 0    monerod
Ctrl-b 1    customer RPC
Ctrl-b 2    merchant RPC
Ctrl-b d    detach without stopping processes
```

## Verify external prerequisites

Codex should verify the environment before live testing.

Check tmux:

```bash
tmux list-windows -t mnp-stagenet
```

Check processes:

```bash
pgrep -af monerod
pgrep -af monero-wallet-rpc
```

Check ports:

```bash
ss -ltnp | grep -E '38081|30185|38084'
```

Expected:

```text
38081  monerod
30185  customer wallet RPC
38084  merchant wallet RPC
```

Verify the daemon:

```bash
curl -s http://127.0.0.1:38081/get_height
```

The response must contain:

```text
"status": "OK"
```

Verify that Stagenet is active:

```bash
curl -s http://127.0.0.1:38081/get_info
```

Expected fields include:

```text
"stagenet": true
"mainnet": false
```

Verify the customer wallet RPC:

```bash
export RPC_HOST=127.0.0.1
export RPC_PORT=30185
export RPC_USER=customer
export RPC_PASSWORD=customer-test

curl --digest -s \
    -u "$RPC_USER:$RPC_PASSWORD" \
    -H 'Content-Type: application/json' \
    -d '{
        "jsonrpc":"2.0",
        "id":"0",
        "method":"get_balance",
        "params":{"account_index":0}
    }' \
    "http://$RPC_HOST:$RPC_PORT/json_rpc"
```

The response must contain a JSON-RPC `result`.

Verify the customer runtime mnp configuration exists:

```bash
test -f tests/.runtime/customer.mnp.ini
```

Verify only non-secret values:

```bash
grep -E '^(host|port|user)[[:space:]]*=' \
    tests/.runtime/customer.mnp.ini
```

Expected:

```text
user = customer
host = 127.0.0.1
port = 30185
```

Do not print the customer RPC password in reports.

Verify the merchant runtime `tx-notify`:

```bash
grep '^tx-notify=' \
    tests/.runtime/merchant-wallet-rpc.conf
```

It must point to:

```text
tests/.build/mnp
```

If any required external service is unavailable:

```text
offline tests: may continue
live tests:    SKIP
```

Codex must not attempt to restart the missing external process.

## Network

All live tests use:

```text
stagenet
```

Set:

```bash
export MNP_TEST_NETWORK=stagenet
```

Never submit transactions without this declaration.

Never run the integration suite on mainnet.

## Build

Run from the repository root:

```bash
rm -rf tests/.build

cmake \
    -S . \
    -B tests/.build \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build tests/.build --parallel
```

Use the freshly built executable:

```bash
export MNP="$PWD/tests/.build/mnp"
export PATH="$PWD/tests/.build:$PATH"
```

Verify:

```bash
"$MNP" version
"$MNP" --version
"$MNP" help
```

A build failure ends the test run.

## Offline regression tests

Run before live tests:

```bash
ctest \
    --test-dir tests/.build \
    --output-on-failure
```

Run:

```bash
python3 tests/offline_test.py
```

Check all shell scripts:

```bash
for file in tests/*.sh; do
    bash -n "$file" || exit 1
done
```

The offline suite uses the freshly built executable for help/version and
argument errors.

It also tests the shell harness with fake RPC/database commands and real local
FIFOs, including:

```text
delayed FIFO creation
multiple FIFOs
timeouts
amount mismatches
JSON-RPC failures
exact atomic-unit conversion
confirmation polling
omitted zero confirmation count
```

No wallet, daemon or MariaDB service should be required by the offline suite.

## Verify mnp against the merchant wallet

The default active mnp configuration must use:

```text
127.0.0.1:38084
```

Verify:

```bash
"$MNP" bc-height
"$MNP" balance
"$MNP" balance --unlocked
"$MNP" payment list
```

All commands must exit with status `0`.

Typical successful output:

```text
mnp bc-height
    numeric blockchain height

mnp balance
    numeric atomic-unit balance

mnp balance --unlocked
    numeric unlocked balance

mnp payment list
    subaddress index and address rows
```

If `mnp` reports:

```text
could not connect to host 127.0.0.1:18084
```

the test configuration is incorrect.

Do not continue live tests until the default mnp configuration points to port
`38084`.

## Verify mnp against the customer wallet

Before successful transfer tests, verify `--config` reaches the spend-capable
customer wallet.

Run:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    balance
```

Expected:

```text
numeric atomic-unit customer balance
exit 0
```

Run:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    bc-height
```

Expected:

```text
numeric blockchain height
exit 0
```

The customer balance should differ from the merchant balance when the wallets
contain different funds.

If `--config` still reaches the merchant wallet, successful transfer tests must
not run.

## MariaDB

The live payment harness uses MariaDB.

Verify:

```bash
mariadb -e "SELECT 1;"
```

The test scripts use `~/.my.cnf`, so credentials do not need to appear in
commands.

Example:

```text
[client]
user=sysadmin
password=mypassword
host=localhost
```

Protect it with:

```bash
chmod 600 ~/.my.cnf
```

Create or update the test database:

```bash
mariadb < tests/create_db.sql
```

Database setup must be repeatable.

It must not require manually inserted sample rows.

Before tests that create payment requests, record the current highest payment
ID:

```bash
before="$(
    mariadb \
        --batch \
        --skip-column-names \
        -e 'SELECT COALESCE(MAX(PAYID),0) FROM payDB.payments;'
)"
```

Only rows with:

```text
PAYID > before
```

should be used to judge the current run.

Show all records:

```bash
./tests/SELECT_ALL.sh
```

or:

```bash
mariadb --table \
    -e "SELECT * FROM payDB.payments;"
```

## Payment states

| Status      | Meaning                                                              |
| ----------- | -------------------------------------------------------------------- |
| `REQUEST`   | Payment request was created.                                         |
| `WAITING`   | Matching payment was detected and the amount FIFO is being read.     |
| `TIMEOUT`   | FIFO read failed or exceeded its timeout.                            |
| `FAILED`    | URI creation failed or the received amount was invalid or incorrect. |
| `COMPLETED` | Received atomic amount matches the expected amount.                  |

## Initialize mnp

Run:

```bash
"$MNP" init
```

Do not run:

```bash
"$MNP" cleanup
```

while transaction detection or FIFO readers are active.

## Start transaction detection

Codex should start the database transaction detector itself.

From the repository root:

```bash
cd tests

./txdetectdb.sh >txdetectdb.log 2>&1 &
TXDETECT_PID=$!
```

It watches the configured mnp transaction directory.

The default path is:

```text
/tmp/mywallet/transactions
```

If the configured work directory differs, use that path instead.

`PIPE_TIMEOUT` defaults to:

```text
90m
```

`SCAN_INTERVAL` defaults to:

```text
0.2
```

Only one detector should read the amount FIFOs.

FIFOs deliver bytes to readers; they are not broadcast channels.

## Drain txid notifications

Codex should also drain the `txid` FIFO:

```bash
while true; do
    cat /tmp/mywallet/txid
done >txid.log 2>&1 &

TXID_READER_PID=$!
```

Use the configured work directory if it is not `/tmp/mywallet`.

Current `mnp` behavior can leave blocking FIFO writer processes when no reader
exists, so the notification pipe must be consumed during transaction tests.

Consume alert pipes only when the corresponding retry/double-spend scenario is
being tested.

## Basic CLI tests

Run:

```bash
"$MNP" version
"$MNP" --version
"$MNP" help
"$MNP" --help

"$MNP" init

"$MNP" cleanup help

"$MNP" balance help
"$MNP" balance
"$MNP" balance --unlocked

"$MNP" bc-height

"$MNP" payment help
"$MNP" transactions --help
"$MNP" sign --help
"$MNP" verify --help
"$MNP" transfer --help
"$MNP" spend-proof help
"$MNP" tx-proof help
```

Unexpected nonzero exit statuses are failures.

Do not run the actual cleanup command until helper processes have been stopped.

## Global configuration option tests

Verify that `--config` works before the command name.

Customer balance:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    balance
```

Customer height:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    bc-height
```

Missing argument:

```bash
"$MNP" --config
```

Expected:

```text
nonzero exit status
```

A nonexistent configuration path must also fail when a command attempts to load
it:

```bash
"$MNP" \
    --config /tmp/mnp-config-does-not-exist.ini \
    balance
```

Expected:

```text
nonzero exit status
```

## Payment tests

Run:

```bash
"$MNP" payment new
"$MNP" payment list
"$MNP" payment subaddr 0
"$MNP" payment subaddr 1 --amount 9999999
```

Integrated payment ID:

```bash
"$MNP" payment e02c381aa2227436
```

stdin:

```bash
printf '%s\n' e02c381aa2227436 |
    "$MNP" payment
```

Invalid subaddress index:

```bash
"$MNP" payment subaddr 999
```

Expected:

```text
nonzero exit status
```

When the merchant wallet RPC is reachable, an invalid subaddress must not be
reported as a network connection failure.

## Transactions tests

Run:

```bash
"$MNP" transactions
"$MNP" transactions --in
"$MNP" transactions --out
"$MNP" transactions --pending
"$MNP" transactions --failed
"$MNP" transactions --pool
```

Expected header:

```text
TYPE	TXID	AMOUNT	CONFIRMATIONS	HEIGHT	LOCKED	ADDRESS	SUBADDR_INDEX
```

Transactions must be globally sorted by `HEIGHT`, newest first.

Verify:

```bash
"$MNP" transactions |
    tail -n +2 |
    awk -F '\t' '
        NR > 1 && $5 > previous {
            exit 1
        }

        {
            previous = $5
        }
    '
```

Multi-destination outgoing transactions may contain multiple rows with the same
TXID.

## Sign and verify

Use a merchant address:

```bash
ADDRESS="$("$MNP" payment subaddr 0)"
MESSAGE="mnp integration test"
```

Sign:

```bash
SIGNATURE="$(
    printf '%s\n' "$MESSAGE" |
        "$MNP" sign
)"
```

Verify:

```bash
printf '%s\n' "$MESSAGE" |
    "$MNP" verify "$ADDRESS" "$SIGNATURE"
```

Expected:

```text
stdout: true
exit:   0
```

Negative test:

```bash
printf '%s\n' "${MESSAGE}x" |
    "$MNP" verify "$ADDRESS" "$SIGNATURE"
```

Expected:

```text
stdout: false
exit:   nonzero
```

Run the supplemental script when present:

```bash
MNP="$MNP" bash tests/sign_verify_test.sh
```

Do not compare separately generated signatures for byte equality.

Two independently generated valid signatures for the same message may differ.

Trailing-LF behavior must be tested by verifying the generated signature
against the normalized message.

## Create a payment request

From `tests/`:

```bash
URI="$(./create_uri.sh 55555)"
```

Assert:

```bash
case "$URI" in
    monero:*)
        ;;
    *)
        exit 1
        ;;
esac
```

The newly created database row should initially have:

```text
REQUEST
```

The database harness covers integrated payment IDs.

Subaddress-based payment requests should be exercised separately using
address-named FIFO consumers.

## Transfer tests

Transfer tests spend real Stagenet XMR from `customerwallet`.

Use only small atomic-unit amounts.

Every successful `mnp transfer` test MUST use:

```text
--config tests/.runtime/customer.mnp.ini
```

The default merchant configuration is view-only and must not be used for
successful transfer submission.

### Verify customer transfer configuration

Before submitting any transaction:

```bash
test -f tests/.runtime/customer.mnp.ini
```

Verify:

```bash
grep -E '^(host|port|user)[[:space:]]*=' \
    tests/.runtime/customer.mnp.ini
```

Expected:

```text
user = customer
host = 127.0.0.1
port = 30185
```

Verify access:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    balance
```

Do not print the password from the runtime configuration.

### Payment URI

Send a merchant payment URI through the customer wallet:

```bash
TXID="$(
    "$MNP" \
        --config tests/.runtime/customer.mnp.ini \
        transfer "$URI"
)"
```

A successful transfer must:

```text
exit 0
write one TXID to stdout
write no diagnostic to stderr
```

Validate:

```bash
[[ "$TXID" =~ ^[0-9a-f]{64}$ ]]
```

Do not submit another transaction merely because Stagenet confirmation is slow.

### Payment URI through stdin

Create a distinct URI:

```bash
URI="$(./create_uri.sh 55556)"
```

Send:

```bash
TXID="$(
    printf '%s\n' "$URI" |
        "$MNP" \
            --config tests/.runtime/customer.mnp.ini \
            transfer
)"
```

Validate:

```bash
[[ "$TXID" =~ ^[0-9a-f]{64}$ ]]
```

Do not use:

```bash
mnp payment new --amount 55555 | mnp transfer
```

as the primary end-to-end test because it creates a destination from the same
merchant wallet and therefore sends funds back to that wallet.

### Explicit address and amount

Use a valid Stagenet destination:

```bash
TXID="$(
    "$MNP" \
        --config tests/.runtime/customer.mnp.ini \
        transfer \
        --address "$DESTINATION" \
        --amount 777
)"
```

Expected:

```text
one TXID
```

The amount is specified in atomic units.

### Multiple destinations

Use normal Stagenet addresses:

```bash
TXID="$(
    "$MNP" \
        --config tests/.runtime/customer.mnp.ini \
        transfer \
        --address "$DESTINATION_1" --amount 777 \
        --address "$DESTINATION_2" --amount 888
)"
```

Expected:

```text
one TXID
```

When visible in customer outgoing history:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    transactions --out |
    grep -F "$TXID"
```

There should be one row per destination using the same TXID.

A new transfer may initially appear in another state.

Check when necessary:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    transactions --pending

"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    transactions --pool

"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    transactions --failed
```

### Payment-ID safety

A single integrated-address transfer is allowed.

An integrated address combined with another destination must fail:

```bash
"$MNP" \
    --config tests/.runtime/customer.mnp.ini \
    transfer \
    --address "$INTEGRATED_ADDRESS" --amount 777 \
    --address "$DESTINATION_1" --amount 888
```

Expected stderr:

```text
mnp transfer: payment ID requires a single destination
```

Expected:

```text
nonzero exit status
```

## End-to-end payment test

Set:

```bash
export RPC_HOST=127.0.0.1
export RPC_PORT=30185
export RPC_USER=customer
export RPC_PASSWORD=customer-test
export MNP_TEST_NETWORK=stagenet
```

With the detector and txid reader running:

```bash
set -o pipefail

./create_uri.sh 55555 |
    ./send_monero.sh
```

A successful sender exit only means the RPC returned a transaction hash.

It does not mean payment detection completed.

For a full automated assertion:

```bash
./end_to_end.sh 55555
```

The test passes only when the database row created by the current run reaches:

```text
COMPLETED
```

The default receipt timeout is:

```text
600 seconds
```

A timeout does not cancel an already submitted transaction.

Do not submit another transaction merely because Stagenet has not mined the
previous one yet.

### End-to-end payment using mnp transfer

Create a merchant payment request and submit it through the customer runtime
configuration:

```bash
PAYMENT_URI="$(./create_uri.sh 55555)"

TXID="$(
    "$MNP" \
        --config "$PWD/.runtime/customer.mnp.ini" \
        transfer "$PAYMENT_URI"
)"
```

When running from the repository root instead of `tests/`, use:

```bash
TXID="$(
    "$MNP" \
        --config tests/.runtime/customer.mnp.ini \
        transfer "$PAYMENT_URI"
)"
```

Validate:

```bash
[[ "$TXID" =~ ^[0-9a-f]{64}$ ]]
```

Then wait with a bounded deadline for the database row created by the current
run to reach:

```text
COMPLETED
```

Submission success alone is not sufficient.

## Monitor tests

Use transactions created during the current run.

Prefer a distinct TXID for each monitor scenario.

Defaults are:

```text
--notify-at 2
--confirmation 0
```

Notification modes:

```text
0  record TXID without notification
1  txpool
2  confirmed
3  unlocked
```

Examples:

```bash
timeout 120 \
    "$MNP" "$TXID" \
    --notify-at 1
```

Confirmed:

```bash
timeout 300 \
    "$MNP" "$TXID" \
    --notify-at 2 \
    --confirmation 1
```

stdin:

```bash
printf '%s\n' "$TXID" |
    timeout 300 \
        "$MNP" \
        --notify-at 2 \
        --confirmation 1
```

Retry:

```bash
printf '%s\n' "$TXID" |
    timeout 300 \
        "$MNP" \
        --retry \
        --notify-at 2 \
        --confirmation 1
```

The monitor records TXIDs before polling.

Do not reuse the same ordinary TXID expecting a second notification-mode test.

Use either:

```text
a new TXID
```

or deliberately:

```text
--retry
```

with the corresponding FIFO handling reset.

Never run confirmation-dependent tests without a timeout.

## RPC retry

With a transaction ID received through:

```text
rpc_connection_alert
```

run:

```bash
timeout 300 \
    "$MNP" --retry \
    < /tmp/mywallet/rpc_connection_alert
```

Optional:

```bash
timeout 300 \
    "$MNP" \
    --retry \
    --notify-at 2 \
    --confirmation 1 \
    < /tmp/mywallet/rpc_connection_alert
```

Only run this scenario when a valid current-run alert exists.

Do not intentionally stop external Monero services to manufacture an alert
unless explicitly instructed by the developer.

## Proof tests

Run proof tests only when matching valid proof data exists.

Spend proof:

```bash
"$MNP" spend-proof \
    "$TXID" \
    --signature "$SIGNATURE"
```

stdin:

```bash
printf '%s\n' "$TXID" |
    "$MNP" spend-proof \
        --signature "$SIGNATURE"
```

Transaction proof:

```bash
"$MNP" tx-proof \
    "$TXID" \
    --address "$ADDRESS" \
    --signature "$SIGNATURE"
```

stdin:

```bash
printf '%s\n' "$TXID" |
    "$MNP" tx-proof \
        --address "$ADDRESS" \
        --signature "$SIGNATURE"
```

Valid proof:

```text
stdout: true
exit:   0
```

Invalid proof:

```text
stdout: false
exit:   nonzero
```

Proof files in this directory are local fixtures and are not standalone
executable tests.

Use them only with their matching:

```text
TXID
address
message
```

Do not fabricate proof data.

If matching proof information is unavailable:

```text
SKIP
```

and report the reason.

## Error tests

These commands must fail:

```bash
"$MNP" invalid-command
"$MNP" balance foo
"$MNP" bc-height foo
"$MNP" payment foo
"$MNP" payment subaddr -1
"$MNP" payment subaddr 999
"$MNP" spend-proof
"$MNP" tx-proof
"$MNP" --version foo
"$MNP" --help foo
"$MNP" --config
```

Each command must return a nonzero exit status.

With a valid receiver configuration:

```text
mnp invalid-command
```

is interpreted as a possible transaction ID and should fail with an invalid
TXID error.

## stdout, stderr and exit status

Verify that commands intended for command substitution write their values to
stdout and diagnostics to stderr.

Examples:

```bash
"$MNP" balance
echo $?
```

```bash
balance="$("$MNP" balance)"
printf 'balance=%s\n' "$balance"
```

Unlocked balance:

```bash
unlocked="$("$MNP" balance --unlocked)"
printf 'unlocked=%s\n' "$unlocked"
```

Height:

```bash
height="$("$MNP" bc-height)"
printf 'height=%s\n' "$height"
```

Customer balance through explicit configuration:

```bash
customer_balance="$(
    "$MNP" \
        --config tests/.runtime/customer.mnp.ini \
        balance
)"

printf 'customer_balance=%s\n' "$customer_balance"
```

## Load testing

Run load tests only after the normal integration tests pass.

### RPC load

Start small:

```bash
JOBS=2 REQUESTS=4 ./load_test.sh rpc
```

Increase gradually:

```bash
JOBS=20 REQUESTS=500 ./load_test.sh rpc
```

Then:

```bash
JOBS=50 REQUESTS=1000 ./load_test.sh rpc
```

`rpc` validates numeric output from:

```text
balance
unlocked balance
blockchain height
```

Each job performs three sequential requests.

## Transaction load testing

Start with a small transaction batch:

```bash
JOBS=2 \
REQUESTS=4 \
AMOUNT=55555 \
./load_test.sh send
```

The `send` mode submits real Stagenet transactions through the customer wallet
RPC.

Submission success alone is not receipt success.

Inspect newly created rows only.

Useful diagnostics:

```bash
mariadb --table \
    -e "SELECT STATUS, COUNT(*) FROM payDB.payments GROUP BY STATUS;"
```

Incomplete or failed:

```bash
mariadb --table \
    -e "SELECT PAYID, AMOUNT, STATUS FROM payDB.payments WHERE STATUS != 'COMPLETED';"
```

These aggregate queries may include previous runs and must not be used as the
only automated assertion.

## Ten-payment Stagenet run

After smaller tests pass:

```bash
export AMOUNT=55555

before="$(
    mariadb \
        --batch \
        --skip-column-names \
        -e 'SELECT COALESCE(MAX(PAYID),0) FROM payDB.payments;'
)"

JOBS=5 \
REQUESTS=10 \
AMOUNT="$AMOUNT" \
./load_test.sh send

after=$((before + 10))
```

Expected sender summary:

```text
success: 10
failed: 0
```

Wait for newly created rows only:

```bash
deadline=$((SECONDS + 600))

while ((SECONDS < deadline)); do
    mariadb --table \
        -e "SELECT PAYID, AMOUNT, STATUS
            FROM payDB.payments
            WHERE PAYID > $before
              AND PAYID <= $after
            ORDER BY PAYID;"

    pending="$(
        mariadb \
            --batch \
            --skip-column-names \
            -e "SELECT COUNT(*)
                FROM payDB.payments
                WHERE PAYID > $before
                  AND PAYID <= $after
                  AND STATUS NOT IN ('COMPLETED','FAILED','TIMEOUT');"
    )"

    ((pending == 0)) && break

    sleep 5
done
```

The run passes when all ten newly created rows contain:

```text
AMOUNT = 55555
STATUS = COMPLETED
```

Do not submit a second batch merely because mining is delayed.

## Script syntax check

Before committing test-script changes:

```bash
for file in tests/*.sh; do
    bash -n "$file" || exit 1
done
```

## Codex cleanup

Codex must terminate only helper processes that Codex started.

For example:

```bash
kill "$TXDETECT_PID" 2>/dev/null || true
kill "$TXID_READER_PID" 2>/dev/null || true
```

Wait for them to exit.

Only run:

```bash
"$MNP" cleanup
```

when the work directory and its FIFOs are owned by the current test run.

If pre-existing external monitor processes or shared FIFOs are still using the
configured work directory:

```text
SKIP cleanup
```

and report the reason.

Codex MUST NOT stop:

```text
monerod
customerwallet monero-wallet-rpc
merchant monero-wallet-rpc
tmux session mnp-stagenet
```

Those are owned by the developer.

## Stop the Stagenet environment

After Codex has completed testing and its own cleanup, the developer may stop
the external environment with:

```bash
./tests/stop_stagenet.sh
```

The stop script must only stop processes belonging to the `mnp-stagenet` tmux
session.

It must not use broad commands such as:

```text
pkill monerod
pkill monero-wallet-rpc
killall monerod
killall monero-wallet-rpc
```

After stopping, these ports should be free:

```text
38081
30185
38084
```

## Failure reporting

For each failed test Codex should report:

```text
TEST:
COMMAND:
EXIT STATUS:
STDOUT:
STDERR:
EXPECTED:
```

Codex must not silently modify production source code to make a failing test
pass.

Source changes require a separate explicit request.

Codex must not commit or push changes unless explicitly requested.

## Skip reporting

Each skipped test must state the exact prerequisite that was missing.

Valid examples include:

```text
matching proof fixture metadata unavailable
no current-run rpc_connection_alert fixture
external service unavailable
shared work directory owned by pre-existing processes
```

The following is no longer a valid skip reason when the runtime customer
configuration exists:

```text
mnp transfer uses the merchant configuration
```

Successful transfer tests must instead use:

```text
--config tests/.runtime/customer.mnp.ini
```

## Final report

At the end, report:

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
customer runtime config availability
merchant tx-notify target
live transactions submitted
```

Each failure or skip must include a short reason.

## Release checklist

Before preparing a release:

* [ ] Increase `VERSION` in `globaldefs.h`
* [ ] Clean Debug build succeeds
* [ ] CTest succeeds
* [ ] Offline regression tests succeed
* [ ] Shell syntax checks succeed
* [ ] Basic CLI tests succeed
* [ ] `--config` override tests succeed
* [ ] Payment tests succeed
* [ ] Transaction tests succeed
* [ ] Sign/verify tests succeed
* [ ] Transfer tests succeed through customer runtime config
* [ ] End-to-end payment detection succeeds
* [ ] Monitor tests succeed
* [ ] Retry flow succeeds
* [ ] Proof tests succeed or skips are documented
* [ ] Small load tests succeed
* [ ] Transaction load tests succeed

