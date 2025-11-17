#!/bin/bash

IP=127.0.0.1
PORT=18083
read MONERO_URI

# Check if Monero URI is provided as an argument
if [ -z "$MONERO_URI" ]; then
    echo "Usage: $0 <monero_uri>"
    exit 1
fi

# Split MONERO_URI into INTEGRATED_ADDRESS and AMOUNT
IFS='?' read -r INTEGRATED_ADDRESS PARAMS <<< "$MONERO_URI"
IFS='=' read -r -a PARAM <<< "$PARAMS"
AMOUNT="${PARAM[1]}"
AMOUNT=${AMOUNT//.}

# Remove "monero:" prefix from INTEGRATED_ADDRESS
INTEGRATED_ADDRESS="${INTEGRATED_ADDRESS#monero:}"

METHOD="split_integrated_address"
PARAMS='{"integrated_address": "'$INTEGRATED_ADDRESS'"}}'

SPLIT_RESPONSE=$(curl -s \
    -u username:password --digest \
    http://$IP:$PORT/json_rpc \
    -d '{"jsonrpc":"2.0","id":"0","method":"'$METHOD'","params":'"$PARAMS"'}' \
    -H 'Content-Type: application/json')

# Extract payment ID from response
PAYMENT_ID=$(echo "$SPLIT_RESPONSE" | jq -r '.result.payment_id')

# Check if payment ID is empty
if [ -z "$PAYMENT_ID" ]; then
    echo "Failed to extract payment ID from integrated address."
    exit 1
fi

echo $PAYMENT_ID $INTEGRATED_ADDRESS $AMOUNT

