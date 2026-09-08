# Testing Monero Named Pipes

The test environment uses MariaDB to track payment requests and their state while multiple transactions are processed concurrently.

## Build and offline regression tests

From the repository root:

```bash
cmake -S . -B tests/.build -DCMAKE_BUILD_TYPE=Debug
cmake --build tests/.build --parallel
ctest --test-dir tests/.build --output-on-failure
python3 tests/offline_test.py
for file in tests/*.sh; do bash -n "$file" || exit 1; done
```

The offline suite uses the freshly built executable for help/version and argument
errors. It also tests the shell harness with fake RPC/database commands and real
local FIFOs: delayed creation, multiple FIFOs, timeouts, amount mismatches,
JSON-RPC failures and exact atomic-unit conversion. No wallet or MariaDB service
is used. Override `MNP` with an absolute binary path if needed.

Dependencies: CMake, a C99 compiler, libcurl development files, Bash 4+, Python 3,
and GNU coreutils/findutils. Live tests also need MariaDB, curl and Monero.
CTest checks confirmation polling with mock wallet RPC responses, including an
omitted zero confirmation count. Run the Python suite separately as shown above.

## Stagenet prerequisites

Start your dedicated sender and receiver wallets with `monero-wallet-rpc
--stagenet --config-file PATH_TO_CONFIG`, connected to a Stagenet daemon.
Use distinct RPC ports and a funded sender wallet. Keep wallet passwords and RPC
credentials in local configuration; do not add them to the repository.

The receiving wallet RPC must use a notification command pointing to the binary
being tested, for example:

```text
tx-notify=/absolute/path/to/mnp/tests/.build/mnp --notify-at 2 --confirmation 1 %s
```

Configure `~/.mnp.ini` for the receiver RPC and a dedicated test work directory.
`mnp init` creates that directory and its pipes; it does not create the INI file.
Only run `mnp cleanup` after stopping the detector and test notifications.

Before live tests, select the binary in every test shell:

```bash
# Run from the repository root.
export MNP="$PWD/tests/.build/mnp"
export PATH="$PWD/tests/.build:$PATH"
export MNP_TEST_NETWORK=stagenet
"$MNP" init
cd tests
```

`MNP_TEST_NETWORK=stagenet` is an explicit operator declaration that both wallets
are configured for Stagenet; it does not discover or change their network.
`send_monero.sh` refuses to submit transfers without this declaration.

The database harness covers integrated payment IDs (`mnp payment PAYMENT_ID`).
It does not map subaddresses from `mnp payment new` or `subaddr` to database rows;
exercise those commands separately with address-named FIFO consumers.

## Why use a database for testing?

A database makes it easier to verify concurrent payments and avoids ambiguity when several transactions are processed at the same time.

## MariaDB setup

Install MariaDB:

```bash
sudo apt update
sudo apt install mariadb-server mariadb-client
```

Start the service:

```bash
sudo systemctl enable --now mariadb
```

Create the test user:

```bash
sudo mariadb
```

```sql
CREATE USER 'sysadmin'@'localhost' IDENTIFIED BY 'mypassword';

GRANT ALL PRIVILEGES ON payDB.* TO 'sysadmin'@'localhost';

FLUSH PRIVILEGES;
```

Exit MariaDB:

```sql
EXIT;
```

## MariaDB client credentials

The test scripts use `~/.my.cnf` so database credentials do not need to be stored in scripts or passed on the command line.

Create:

```bash
cat > ~/.my.cnf <<'EOF'
[client]
user=sysadmin
password=mypassword
host=localhost
EOF
```

Protect the file:

```bash
chmod 600 ~/.my.cnf
```

Test the connection:

```bash
mariadb -e "SELECT 1;"
```

## Create the test database

From the `tests` directory:

```bash
mariadb < create_db.sql
```

Setup is repeatable and does not insert sample rows, reset IDs or remove existing
records. Use newly created payment IDs when judging a test run.

Show all payment records:

```bash
./SELECT_ALL.sh
```

or directly:

```bash
mariadb --table \
    -e "SELECT * FROM payDB.payments;"
```

Illustrative records (not inserted by database setup):

```text
+-------+--------+-----------+
| PAYID | AMOUNT | STATUS    |
+-------+--------+-----------+
|     1 | 500000 | REQUEST   |
|     2 | 787878 | WAITING   |
|     3 | 787878 | TIMEOUT   |
|     4 | 123456 | FAILED    |
|     5 | 523456 | COMPLETED |
+-------+--------+-----------+
```

## Sender wallet RPC

`send_monero.sh` uses a separate sender `monero-wallet-rpc`.

The default endpoint is:

```text
127.0.0.1:30185
```

Set the RPC credentials before running transaction tests:

```bash
export RPC_USER='sender'
export RPC_PASSWORD='rpcpassword'
```

Optional settings:

```bash
export RPC_HOST='127.0.0.1'
export RPC_PORT='30185'
export RPC_ACCOUNT='0'
```

The variables only need to be exported once per shell session.

## Start transaction detection

Start the database transaction detector:

```bash
./txdetectdb.sh
```

It watches:

```text
/tmp/mywallet/transactions
```

and updates the payment status in MariaDB. Override the path with
`WATCHDIR=/your/test/workdir/transactions ./txdetectdb.sh` when needed.
`PIPE_TIMEOUT` defaults to `90m`; `SCAN_INTERVAL` defaults to `0.2` seconds.
The detector scans for all FIFOs repeatedly, including those created after their
parent transaction directory. Run one detector and no competing amount-pipe
readers: FIFOs deliver bytes to readers, not broadcast copies.

For each path, the detector starts one read during its lifetime. Restart it before
replaying the same TXID/FIFO with `--retry`. Unknown payment IDs and address-named
FIFOs are reported and left unread. A detected payment without a database row is
not treated as success.

In another terminal, also drain the test work directory's `txid` notifications:

```bash
while true; do cat /tmp/mywallet/txid; done
```

Current `mnp` forks blocking FIFO writers. Leaving `txid` unread accumulates writer
processes under load. Consume alert pipes as appropriate for retry/double-spend
scenarios too; do not run a retry consumer until you intend to retry.

## Create a payment request

Create a new payment URI:

```bash
./create_uri.sh 55555
```

Example output:

```text
monero:AAkPz3y5yNweDPWW7FZoqd1Nwvhfh2UbAMBR4UeGPi1aWpERgmE3ChMeJZJ2RnkMueHdL7XXwdkQJ5As8XRhTKAhfJb3BrWxFGT1maXEsT?tx_amount=0.000000055555
```

Check the database:

```bash
./SELECT_ALL.sh
```

The new request initially has:

```text
REQUEST
```

## End-to-end payment test

With the sender wallet RPC running and the environment variables configured:

```bash
set -o pipefail
./create_uri.sh 55555 | ./send_monero.sh
```

A successful sender exit means the RPC returned a transaction hash; it does not
mean the receiver completed the payment. Check the new database row until it
reaches:

```text
COMPLETED
```

For an automated receipt assertion against the specific newly created payment:

```bash
./end_to_end.sh 55555
```

This uses `split_URI.sh` to recover the ID, submits one transfer, and polls that
row until `COMPLETED`. It exits nonzero for a failed state, missing row, RPC error,
or receipt timeout (`RECEIPT_TIMEOUT=600` seconds by default). Start the detector
and `txid` consumer first. A timeout does not cancel an already submitted transfer.

## Payment states

| Status | Meaning |
|---|---|
| `REQUEST` | Payment request created by `create_uri.sh`. |
| `WAITING` | Matching integrated payment ID detected and the transaction amount FIFO is being read. |
| `TIMEOUT` | FIFO read failed or exceeded the configured timeout. |
| `FAILED` | URI creation failed, or the received amount is invalid or differs from the expected amount. |
| `COMPLETED` | Received numeric amount matches the expected atomic amount. |

## Test checklist

### Basic

- [ ] `mnp version`
- [ ] `mnp --version`
- [ ] `mnp help`
- [ ] `mnp --help`
- [ ] `mnp init`
- [ ] `mnp cleanup help`
- [ ] `mnp cleanup`
- [ ] `mnp balance help`
- [ ] `mnp balance`
- [ ] `mnp balance --unlocked`
- [ ] `mnp bc-height`

### Payment

- [ ] `mnp payment help`
- [ ] `mnp payment new`
- [ ] `mnp payment list`
- [ ] `mnp payment subaddr 0`
- [ ] `mnp payment subaddr 1 --amount 9999999`
- [ ] `mnp payment e02c381aa2227436`
- [ ] `echo e02c381aa2227436 | mnp payment`

### Monitor

Use a distinct TXID for each scenario, or deliberately use `--retry` with a
restarted detector and drained pipes. The monitor remembers TXIDs before polling;
a second ordinary invocation for the same TXID exits successfully without testing
a new notification mode. Defaults are `--notify-at 2 --confirmation 0`.
`--notify-at 0` records the TXID without notifying; txpool notification is `1`.


- [ ] `mnp "$TXID"`
- [ ] `mnp "$TXID" --notify-at 1`
- [ ] `mnp "$TXID" --notify-at 2 --confirmation 1`
- [ ] `mnp "$TXID" --notify-at 3`
- [ ] `echo "$TXID" | mnp`
- [ ] `echo "$TXID" | mnp --notify-at 2 --confirmation 1`
- [ ] `echo "$TXID" | mnp --retry`
- [ ] `echo "$TXID" | mnp --retry --notify-at 2 --confirmation 1`

### RPC retry

With a transaction ID received through `rpc_connection_alert`:

```bash
mnp --retry < /tmp/mywallet/rpc_connection_alert
```

Optional monitor settings:

```bash
mnp --retry --notify-at 2 --confirmation 1 \
    < /tmp/mywallet/rpc_connection_alert
```

### Proofs

- [ ] `mnp spend-proof help`
- [ ] `mnp tx-proof help`
- [ ] `mnp spend-proof "$TXID" --signature "$SIGNATURE"`
- [ ] `echo "$TXID" | mnp spend-proof --signature "$SIGNATURE"`
- [ ] `mnp tx-proof "$TXID" --address "$ADDRESS" --signature "$SIGNATURE"`
- [ ] `echo "$TXID" | mnp tx-proof --address "$ADDRESS" --signature "$SIGNATURE"`

Proof files in this directory are local fixtures, not executable tests. Load a
signature with `SIGNATURE="$(cat monero_spend_proof)"` (or `monero_tx_proof`) and
supply its matching TXID/address/message. A mismatched proof should print `false`
and exit nonzero; malformed input/RPC failure should also fail. The offline suite
does not claim to verify real cryptographic proofs.

A valid proof prints:

```text
true
```

and exits with status `0`.

### Errors

- [ ] `mnp invalid-command`
- [ ] `mnp balance foo`
- [ ] `mnp bc-height foo`
- [ ] `mnp payment foo`
- [ ] `mnp payment subaddr -1`
- [ ] `mnp spend-proof`
- [ ] `mnp tx-proof`
- [ ] `mnp --version foo`
- [ ] `mnp --help foo`

With a valid receiver INI configuration, `mnp invalid-command` is interpreted as a possible transaction ID and should therefore fail with an invalid TXID error.

### stdout / stderr / exit status

```bash
mnp balance
echo $?
```

```bash
mnp balance --unlocked
echo $?
```

```bash
mnp bc-height
echo $?
```

```bash
mnp spend-proof "$TXID" --signature "$SIGNATURE"
echo $?
```

Test command substitution:

```bash
balance="$(mnp balance)"
printf 'balance=%s\n' "$balance"
```

```bash
unlocked="$(mnp balance --unlocked)"
printf 'unlocked=%s\n' "$unlocked"
```

```bash
height="$(mnp bc-height)"
printf 'height=%s\n' "$height"
```

## Load testing

`rpc` validates numeric output from balance, unlocked balance and height. Each
job makes three sequential requests. `payment` creates requests without sending;
`send` checks submission only. `all` runs `rpc` then `payment`, never `send`.
All modes honor `MNP`; `JOBS` and `REQUESTS` must be positive integers.

Run concurrent wallet RPC requests:

```bash
JOBS=20 REQUESTS=500 ./load_test.sh rpc
```

Increase the load gradually:

```bash
JOBS=50 REQUESTS=1000 ./load_test.sh rpc
```

Check the number of successful and failed requests after each run.

## End-to-end transaction load test

After a single payment has reached COMPLETED, start with a small submission batch:

```bash
JOBS=2 REQUESTS=4 AMOUNT=55555 ./load_test.sh send
```

Wait for receipt and inspect the newly inserted rows. These aggregate queries also
include previous runs, so they are diagnostics, not automated assertions:

```bash
mariadb --table \
    -e "SELECT STATUS, COUNT(*) FROM payDB.payments GROUP BY STATUS;"
```

Show failed or incomplete payments:

```bash
mariadb --table \
    -e "SELECT PAYID, AMOUNT, STATUS FROM payDB.payments WHERE STATUS != 'COMPLETED';"
```

### Reproduce the 10-payment Stagenet run

This recipe submits ten payments of 55,555 atomic units with five workers and
then verifies every newly created database row. Use dedicated Stagenet wallets
and a funded sender; the `send` mode creates real transactions.

From the repository root, build and select the test binary:

```bash
cmake -S . -B tests/.build -DCMAKE_BUILD_TYPE=Debug
cmake --build tests/.build --parallel
export MNP="$PWD/tests/.build/mnp"
export MNP_TEST_NETWORK=stagenet
export RPC_HOST=127.0.0.1
export RPC_PORT=30185
export RPC_USER='sender-rpc-user'
export RPC_PASSWORD='sender-rpc-password'
export AMOUNT=55555
```

Initialize the receiver work directory once, then use two additional terminals
from `tests/` to keep the notification pipes drained:

```bash
"$MNP" init
./txdetectdb.sh
```

```bash
while true; do cat /tmp/mywallet/txid; done
```

In a third terminal, record the current highest payment ID and submit the batch:

```bash
cd /path/to/mnp/tests
before=$(mariadb --batch --skip-column-names \
    -e 'SELECT COALESCE(MAX(PAYID),0) FROM payDB.payments;')
JOBS=5 REQUESTS=10 AMOUNT=55555 ./load_test.sh send
after=$((before + 10))
```

The sender summary must report `success: 10` and `failed: 0`. Submission only
means the sender received transaction hashes; wait for Stagenet mining and the
configured confirmation level before checking receipts:

```bash
deadline=$((SECONDS + 600))
while (( SECONDS < deadline )); do
    mariadb --table -e "SELECT PAYID, AMOUNT, STATUS FROM payDB.payments WHERE PAYID > $before AND PAYID <= $after ORDER BY PAYID;"
    pending=$(mariadb --batch --skip-column-names -e "SELECT COUNT(*) FROM payDB.payments WHERE PAYID > $before AND PAYID <= $after AND STATUS NOT IN ('COMPLETED','FAILED','TIMEOUT');")
    (( pending == 0 )) && break
    sleep 5
done
```

The run passes when all ten rows contain `AMOUNT=55555` and
`STATUS=COMPLETED`. If rows remain `REQUEST`, leave `txdetectdb.sh` running and
mine a Stagenet block; do not submit another batch while waiting.

## Script syntax check

Before committing test script changes:

```bash
for file in *.sh; do
    bash -n "$file" || exit 1
done
```

## Release

Before preparing a release:

- [ ] Increase `VERSION` in `globaldefs.h`
- [ ] Clean build succeeds
- [ ] Basic command tests succeed
- [ ] Payment tests succeed
- [ ] Monitor tests succeed
- [ ] Proof tests succeed
- [ ] Retry flow succeeds
- [ ] Load tests succeed
## URI inspection helper

`printf '%s\n' "$URI" | ./split_URI.sh` uses the same `RPC_HOST`, `RPC_PORT`,
`RPC_USER` and `RPC_PASSWORD` variables as the sender. It prints payment ID,
integrated address and exact atomic amount; RPC errors produce a nonzero exit.

## Known coverage limits

The database harness assumes one expected payment per ID and does not aggregate
partial payments or multiple transfers to the same ID. Numeric comparison accepts
cJSON exponent notation, but cannot recover precision already lost by production
code's floating-point JSON representation. Include large-amount cases separately;
do not interpret a passing small-amount load test as proof of uint64 precision.
