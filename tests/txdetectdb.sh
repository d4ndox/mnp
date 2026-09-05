#!/bin/bash

set -u

DB_CMD="mariadb"
WATCHDIR="/tmp/mywallet/transactions"

read_pipe()
{
    local dir="$1"
    local file="$2"
    local amount
    local expected_amount
    local payid

    payid=$(printf "%d" "0x$file")

    echo "$payid"

    "$DB_CMD" \
        -e "UPDATE payDB.payments SET STATUS = 'WAITING' WHERE PAYID = $payid;"

    if ! amount=$(timeout 90m cat "${dir}/${file}"); then
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'TIMEOUT' WHERE PAYID = $payid;"

        return 1
    fi

    echo "Received from $file: $amount"

    expected_amount=$(
        "$DB_CMD" \
            --batch \
            --skip-column-names \
            -e "SELECT AMOUNT FROM payDB.payments WHERE PAYID = $payid;"
    )

    if [ "$amount" = "$expected_amount" ]; then
        echo "amount is equal."

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'COMPLETED' WHERE PAYID = $payid;"
    else
        echo "amount is not equal."

        "$DB_CMD" \
            -e "UPDATE payDB.payments SET STATUS = 'FAILED' WHERE PAYID = $payid;"
    fi
}

cleanup()
{
    local jobs

    jobs=$(jobs -p)

    if [ -n "$jobs" ]; then
        kill $jobs 2>/dev/null
    fi

    exit
}

trap cleanup SIGINT SIGTERM

find "$WATCHDIR" -type p 2>/dev/null |
while read -r fifo; do
    dir=$(dirname "$fifo")
    file=$(basename "$fifo")

    read_pipe "$dir" "$file" &
done

inotifywait \
    -m "$WATCHDIR" \
    -e create \
    --format '%w%f' |
while read -r new_path; do
    if [ ! -d "$new_path" ]; then
        continue
    fi

    echo "New txId detected: $new_path"

    pipe_file=$(find "$new_path" -maxdepth 1 -type p -printf '%f\n' | head -n1)

    if [ -n "$pipe_file" ] &&
       [ -p "$new_path/$pipe_file" ]; then {
        echo "New fifo pipe detected: $pipe_file in $new_path"
        read_pipe "$new_path" "$pipe_file" &
    } else {
        echo "No pipe found in $new_path"
    }
    fi
done

