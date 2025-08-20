#!/bin/bash

FILE="/tmp/mywallet/bc_height"

inotifywait -m -e close_write "$FILE" --format '%w %e %f' | while read path action file; do
        bcheight=$(cat "$FILE")
        echo "$bcheight"
done
