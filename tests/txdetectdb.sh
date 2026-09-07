#!/bin/bash

set -u
set -o pipefail

DB_CMD="${DB_CMD:-mariadb}"
WATCHDIR="${WATCHDIR:-/tmp/mywallet/transactions}"
PIPE_TIMEOUT="${PIPE_TIMEOUT:-90m}"
SCAN_INTERVAL="${SCAN_INTERVAL:-0.2}"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

read_pipe()
{
    local dir="$1"
    local file="$2"
    local amount
    local expected_amount
    local payid
    local reader_pid
    local amount_file

    if ! [[ "$file" =~ ^[0-9a-fA-F]{16}$ ]]; then
        echo "txdetectdb: invalid payment ID '$file'" >&2
        return 1
    fi

    if [[ "${file:0:8}" != 00000000 ]]; then
        echo "txdetectdb: payment ID '$file' exceeds the test database ID range" >&2
        return 1
    fi
    payid=$(printf "%d" "0x$file")

    if ! expected_amount=$("$DB_CMD" --batch --skip-column-names \
        -e "SELECT AMOUNT FROM payDB.payments WHERE PAYID = $payid;") ||
       ! [[ "$expected_amount" =~ ^[0-9]+$ ]]; then
        echo "txdetectdb: no valid request for payment $payid" >&2
        return 1
    fi

    echo "Payment ID: $payid"

    if ! "$DB_CMD" \
        -e "UPDATE payDB.payments SET STATUS = 'WAITING' WHERE PAYID = $payid;"; then
        echo "txdetectdb: could not update payment $payid to WAITING" >&2
        return 1
    fi

    amount_file=$(mktemp) || return 1
    trap 'kill "$reader_pid" 2>/dev/null || true; wait "$reader_pid" 2>/dev/null || true; rm -f "$amount_file"' EXIT
    trap 'exit 143' TERM
    trap 'exit 130' INT
    timeout "$PIPE_TIMEOUT" cat "${dir}/${file}" > "$amount_file" &
    reader_pid=$!
    if ! wait "$reader_pid"; then
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'TIMEOUT' WHERE PAYID = $payid;" \
            || true

        return 1
    fi

    amount=$(cat "$amount_file")
    echo "Received from $file: $amount"

    if python3 "$TEST_DIR/test_data.py" compare "$amount" "$expected_amount"; then
        echo "Amount is equal."

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'COMPLETED' WHERE PAYID = $payid;"
    else
        echo "Amount is not equal."

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'FAILED' WHERE PAYID = $payid;"
    fi
}

cleanup()
{
    local pids

    pids=$(jobs -pr)

    if [ -n "$pids" ]; then
        kill $pids 2>/dev/null || true
        wait $pids 2>/dev/null || true
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ ! -d "$WATCHDIR" ]; then
    echo "txdetectdb: watch directory '$WATCHDIR' does not exist" >&2
    exit 1
fi

# Scan repeatedly: mnp creates the directory before its FIFOs and may create
# several FIFOs per transaction. Keep workers in this shell for cleanup.
declare -A seen=()
while true; do
    while IFS= read -r -d '' fifo; do
        if [[ -n "${seen[$fifo]:-}" ]]; then
            continue
        fi
        seen["$fifo"]=1
        read_pipe "$(dirname "$fifo")" "$(basename "$fifo")" &
    done < <(find "$WATCHDIR" -mindepth 2 -maxdepth 2 -type p -print0)
    sleep "$SCAN_INTERVAL"
done
