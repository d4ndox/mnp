#!/bin/bash

set -euo pipefail

RPC_HOST="${RPC_HOST:-127.0.0.1}"
RPC_PORT="${RPC_PORT:-30185}"
RPC_USER="${RPC_USER:-}"
RPC_PASSWORD="${RPC_PASSWORD:-}"
RPC_ACCOUNT="${RPC_ACCOUNT:-0}"

if [ -z "$RPC_USER" ] || [ -z "$RPC_PASSWORD" ]; then
    echo "send_monero: RPC_USER and RPC_PASSWORD are required" >&2
    exit 1
fi

if ! IFS= read -r uri; then
    echo "send_monero: missing Monero URI" >&2
    exit 1
fi

if [[ "$uri" != monero:* ]]; then
    echo "send_monero: invalid Monero URI '$uri'" >&2
    exit 1
fi

address="${uri#monero:}"
query=""

if [[ "$address" == *\?* ]]; then
    query="${address#*\?}"
    address="${address%%\?*}"
fi

if [ -z "$address" ]; then
    echo "send_monero: missing destination address" >&2
    exit 1
fi

tx_amount=""

IFS='&' read -r -a parameters <<< "$query"

for parameter in "${parameters[@]}"; do
    case "$parameter" in
        tx_amount=*)
            tx_amount="${parameter#tx_amount=}"
            ;;
    esac
done

if [ -z "$tx_amount" ]; then
    echo "send_monero: tx_amount missing from URI" >&2
    exit 1
fi

if ! [[ "$tx_amount" =~ ^[0-9]+([.][0-9]{1,12})?$ ]]; then
    echo "send_monero: invalid tx_amount '$tx_amount'" >&2
    exit 1
fi

amount=$(
    awk -v value="$tx_amount" '
        BEGIN {
            printf "%.0f\n", value * 1000000000000
        }
    '
)

if ! [[ "$amount" =~ ^[0-9]+$ ]] || [ "$amount" -eq 0 ]; then
    echo "send_monero: invalid atomic amount '$amount'" >&2
    exit 1
fi

request=$(
    printf \
        '{"jsonrpc":"2.0","id":"0","method":"transfer","params":{"destinations":[{"amount":%s,"address":"%s"}],"account_index":%s,"priority":0}}' \
        "$amount" \
        "$address" \
        "$RPC_ACCOUNT"
)

curl \
    --silent \
    --show-error \
    --fail-with-body \
    --digest \
    --user "${RPC_USER}:${RPC_PASSWORD}" \
    --header 'Content-Type: application/json' \
    --data "$request" \
    "http://${RPC_HOST}:${RPC_PORT}/json_rpc"

printf '\n'

