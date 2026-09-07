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

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "${MNP_TEST_NETWORK:-}" != stagenet ]]; then
    echo "send_monero: set MNP_TEST_NETWORK=stagenet after checking both wallet networks" >&2
    exit 1
fi

request=$(python3 "$TEST_DIR/test_data.py" transfer "$RPC_ACCOUNT")
response=$(curl \
    --silent --show-error --fail-with-body \
    --connect-timeout 5 --max-time 120 \
    --digest --user "${RPC_USER}:${RPC_PASSWORD}" \
    --header 'Content-Type: application/json' \
    --data "$request" \
    "http://${RPC_HOST}:${RPC_PORT}/json_rpc")
printf '%s\n' "$response" | python3 "$TEST_DIR/test_data.py" transfer-result
