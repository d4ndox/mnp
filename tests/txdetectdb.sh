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

# Watch for new pipes using inotifywait
inotifywait -m /tmp/mywallet -e create -r |
while read dir action file; do
    # Check if the created file is a named pipe
    if [ -p "${dir}/${file}" ]; then
        echo "New pipe detected: $file"
        # Call function to read from pipe in a background process
        read_pipe "$dir" "$file" &
    fi
done
