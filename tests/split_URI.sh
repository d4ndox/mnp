#!/bin/bash
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${RPC_USER:?Set RPC_USER locally}"
: "${RPC_PASSWORD:?Set RPC_PASSWORD locally}"
IFS= read -r uri
request=$(printf '%s\n' "$uri" | python3 "$TEST_DIR/test_data.py" split)
response=$(curl --silent --show-error --fail-with-body \
    --connect-timeout 5 --max-time 30 \
    --digest --user "${RPC_USER}:${RPC_PASSWORD}" \
    --header 'Content-Type: application/json' --data "$request" \
    "http://${RPC_HOST:-127.0.0.1}:${RPC_PORT:-30185}/json_rpc")
printf '%s\n' "$response" | python3 "$TEST_DIR/test_data.py" split-result "$uri"
