#!/bin/bash

# Function to handle reading from a pipe
read_pipe() {
    local dir="$1"
    local file="$2"
    local amount
    local payid=$(printf "%d" "0x$file")

    echo $payid
    mysql --user=sysadmin \
          --password=mypassword \
          -e "UPDATE payDB.payments SET STATUS = 'WAITING' WHERE PAYID = $payid;"

    # Set timeout for cat command
    if ! amount=$(timeout 90m cat "${dir}/${file}"); then
        # Handle timeout - write to syslog and exit
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"
        mysql --user=sysadmin \
              --password=mypassword \
              -e "UPDATE payDB.payments SET STATUS = 'TIMEOUT' WHERE PAYID = $payid;"
        exit 1
    fi

    echo "Received from $file: $amount"
    a1=$(mysql --batch \
               --user=sysadmin \
               --password=mypassword \
               -e "SELECT (amount) FROM payDB.payments WHERE PAYID = $payid;" | tail -n1)
    if [ "$amount" = "$a1" ]; then
        echo "amount is equal."
        mysql --user=sysadmin \
              --password=mypassword \
              -e "UPDATE payDB.payments SET STATUS = 'COMPLETED' WHERE PAYID = $payid;"
    else
        echo "amount is not equal."
        mysql --user=sysadmin \
              --password=mypassword \
              -e "UPDATE payDB.payments SET STATUS = 'FAILED' WHERE PAYID = $payid;"
    fi
}

# Trap SIGINT to clean up child processes
trap 'kill $(jobs -p); exit' SIGINT

# 1. At start all existing pipes need to be read.
find "$WATCHDIR" -type p 2>/dev/null | while read fifo; do
    dir=$(dirname "$fifo")
    file=$(basename "$fifo")
    (
        read_pipe "$dir" "$file"
    ) &
done

# Watch for new directories using inotifywait
inotifywait -m /tmp/mywallet/transactions -e create --format '%w%f' |
while read new_path; do
    if [ -d "$new_path" ]; then
        echo "New txId detected: $new_path"

        # Lesen der einzigen Datei im Verzeichnis
        pipe_file=$(ls "$new_path")
        if [ -p "$new_path/$pipe_file" ]; then
            echo "New fifo pipe detected: $pipe_file in $new_path"
            read_pipe "$new_path" "$pipe_file" &
        else
            echo "No pipe found in $new_path"
        fi
    fi
done
