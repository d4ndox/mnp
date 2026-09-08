#!/bin/bash

set -euo pipefail

DB_CMD="${DB_CMD:-mariadb}"
MNP="${MNP:-mnp}"

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 AMOUNT" >&2
    exit 1
fi

amount="$1"

if ! [[ "$amount" =~ ^[0-9]+$ ]]; then
    echo "create_uri: amount must be a non-negative integer" >&2
    exit 1
fi

amount="$(python3 -c 'import sys; print(int(sys.argv[1]))' "$amount")"

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

if ! [[ "$payid" =~ ^[0-9]+$ ]] || (( payid < 1 || payid > 4294967295 )); then
    echo "create_uri: could not retrieve payment ID" >&2
    exit 1
fi

if ! uri=$(printf "%016x\n" "$payid" | "$MNP" payment --amount "$amount"); then
    "$DB_CMD" -e "UPDATE payDB.payments SET STATUS = 'FAILED' WHERE PAYID = $payid;"
    exit 1
fi
printf '%s\n' "$uri"

