#!/bin/bash

# Function to handle reading from a pipe
read_pipe() {
    local dir="$1"
    local file="$2"
    local amount

    # Set timeout for cat command
    if ! amount=$(timeout 125m cat "${dir}/${file}"); then
        # Handle timeout - write to syslog and exit
        echo "Timeout occurred while reading from $file" >&2
        logger "Timeout occurred while reading from $file"
        exit 1
    fi

    echo "Received from $file: $amount"
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
