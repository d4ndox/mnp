#!/bin/bash
WATCHDIR="/tmp/mywallet/transactions"

known=""

# Function to handle reading from a pipe
read_pipe() {
    local dir="$1"
    local file="$2"
    local amount

    # Set timeout for cat command (125 minutes)
    if ! amount=$(timeout 125m cat "${dir}/${file}"); then
        # Handle timeout - write to syslog and exit
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"
        exit 1
    fi

    echo "Received from $file: $amount"
}

while true; do
    for fifo in $(find "$WATCHDIR" -type p 2>/dev/null); do
        if [[ ! "$known" =~ "$fifo" ]]; then
            echo "New transaction detected: $fifo"
            known="$known $fifo"
            (
                read_pipe "$(dirname "$fifo")" "$(basename "$fifo")"
            ) &
        fi
    done
    sleep 1
done
