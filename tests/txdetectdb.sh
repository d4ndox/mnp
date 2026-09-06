#!/bin/bash

set -u
set -o pipefail

DB_CMD="mariadb"
WATCHDIR="/tmp/mywallet/transactions"

read_pipe()
{
    local dir="$1"
    local file="$2"
    local amount
    local expected_amount
    local payid

    if ! [[ "$file" =~ ^[0-9a-fA-F]{16}$ ]]; then
        echo "txdetectdb: invalid payment ID '$file'" >&2
        return 1
    fi

    payid=$(printf "%d" "0x$file")

    echo "Payment ID: $payid"

    if ! "$DB_CMD" \
        -e "UPDATE payDB.payments SET STATUS = 'WAITING' WHERE PAYID = $payid;"; then
        echo "txdetectdb: could not update payment $payid to WAITING" >&2
        return 1
    fi

    if ! amount=$(timeout 90m cat "${dir}/${file}"); then
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'TIMEOUT' WHERE PAYID = $payid;" \
            || true

        return 1
    fi

    echo "Received from $file: $amount"

    if ! expected_amount=$(
        "$DB_CMD" \
            --batch \
            --skip-column-names \
            -e "SELECT AMOUNT FROM payDB.payments WHERE PAYID = $payid;"
    ); then
        echo "txdetectdb: could not query payment $payid" >&2
        return 1
    fi

    if [ "$amount" = "$expected_amount" ]; then
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
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ ! -d "$WATCHDIR" ]; then
    echo "txdetectdb: watch directory '$WATCHDIR' does not exist" >&2
    exit 1
fi

find "$WATCHDIR" -type p -print0 2>/dev/null |
while IFS= read -r -d '' fifo; do
    dir=$(dirname "$fifo")
    file=$(basename "$fifo")

    read_pipe "$dir" "$file" &
done

inotifywait \
    -m "$WATCHDIR" \
    -e create \
    --format '%w%f' |
while IFS= read -r new_path; do
    if [ ! -d "$new_path" ]; then
        continue
    fi

    echo "New txId detected: $new_path"

    pipe_file=$(
        find "$new_path" \
            -maxdepth 1 \
            -type p \
            -printf '%f\n' |
            head -n1
    )

    if [ -n "$pipe_file" ] &&
       [ -p "$new_path/$pipe_file" ]; then
        echo "New fifo pipe detected: $pipe_file in $new_path"
        read_pipe "$new_path" "$pipe_file" &
    else
        echo "No pipe found in $new_path"
    fi
done

