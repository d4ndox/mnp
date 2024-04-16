#!/bin/bash

# Parse the subaddress and amount from the command line argument
echo $SADDR
IP=127.0.0.1
PORT=18085

read ALL
PAYMENT_ID=$(echo $ALL | cut -d" " -f1)
INTEGRATED_ADDRESS=$(echo $ALL | cut -d" " -f2)
AMOUNT=$(echo $ALL | cut -d" " -f3)


# Check if Monero URI is provided as an argument
if [ -z "$PAYMENT_ID" ]; then
    echo "Usage: $0 <payment_id> <integrated address> <amount>"
    exit 1
fi

METHOD="transfer"
PARAMS='{"destinations":[{"amount":'$AMOUNT',"address":"'$INTEGRATED_ADDRESS'","payment_id":"'$PAYMENT_ID'"}],"account_index":0,"subaddr_indices":[0],"priority":0}}'

echo $METHOD
echo $PARAMS
curl \
    -u username:password --digest \
    http://$IP:$PORT/json_rpc \
    -d '{"jsonrpc":"2.0","id":"0","method":"'$METHOD'","params":'"$PARAMS"'}' \
    -H 'Content-Type: application/json'
