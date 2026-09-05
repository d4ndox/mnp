#!/bin/bash

set -euo pipefail

DB_CMD="mariadb"

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 AMOUNT" >&2
    exit 1
fi

amount="$1"

if ! [[ "$amount" =~ ^[0-9]+$ ]]; then
    echo "create_uri: amount must be a non-negative integer" >&2
    exit 1
fi

payid=$(
    "$DB_CMD" \
        --batch \
        --skip-column-names \
        -e "
            INSERT INTO payDB.payments (AMOUNT, STATUS)
            VALUES ('$amount', 'REQUEST');
            SELECT LAST_INSERT_ID();
        "
)

if [ -z "$payid" ]; then
    echo "create_uri: could not retrieve payment ID" >&2
    exit 1
fi

printf "%016x\n" "$payid" |
    mnp payment --amount "$amount"

