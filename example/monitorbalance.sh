#!/bin/bash

FILE="/tmp/mywallet/balance"

inotifywait -m -e close_write "$FILE" --format '%w %e %f' | while read path action file; do
        balance_piconero=$(cat "$FILE")
        balance_monero=$(echo "scale=12; $balance_piconero / 1000000000000" | bc)
        echo "$balance_monero"
done
