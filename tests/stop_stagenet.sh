#!/usr/bin/env bash
# tests/stop_stagenet.sh

set -euo pipefail

SESSION="${SESSION:-mnp-stagenet}"
STOP_TIMEOUT="${STOP_TIMEOUT:-10}"

DAEMON_PORT="${DAEMON_PORT:-38081}"
CUSTOMER_RPC_PORT="${CUSTOMER_RPC_PORT:-30185}"
MERCHANT_RPC_PORT="${MERCHANT_RPC_PORT:-38084}"

die()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 ||
        die "required command not found: $1"
}

session_exists()
{
    tmux has-session -t "$SESSION" 2>/dev/null
}

window_exists()
{
    local window="$1"

    tmux list-windows \
        -t "$SESSION" \
        -F '#W' 2>/dev/null |
        grep -Fxq "$window"
}

send_interrupt()
{
    local window="$1"

    if window_exists "$window"; then
        printf 'Stopping %-10s ...\n' "$window"

        tmux send-keys \
            -t "$SESSION:$window" \
            C-c
    fi
}

wait_for_session()
{
    local deadline=$((SECONDS + STOP_TIMEOUT))

    while session_exists; do
        if ((SECONDS >= deadline)); then
            return 1
        fi

        sleep 0.2
    done

    return 0
}

port_in_use()
{
    local port="$1"

    ss -ltnH |
        awk '{print $4}' |
        grep -Eq "[:.]${port}$"
}

print_port_status()
{
    local name="$1"
    local port="$2"

    if port_in_use "$port"; then
        printf '  [BUSY] %s :%s\n' "$name" "$port"
    else
        printf '  [FREE] %s :%s\n' "$name" "$port"
    fi
}

main()
{
    require_command tmux
    require_command ss
    require_command awk
    require_command grep

    if ! session_exists; then
        printf "tmux session '%s' is not running.\n" "$SESSION"

        printf '\nPort status:\n'
        print_port_status monerod "$DAEMON_PORT"
        print_port_status customer "$CUSTOMER_RPC_PORT"
        print_port_status merchant "$MERCHANT_RPC_PORT"

        exit 0
    fi

    printf "Stopping Stagenet session '%s'...\n\n" "$SESSION"

    send_interrupt merchant
    send_interrupt customer
    send_interrupt monerod

    if ! wait_for_session; then
        printf '\nProcesses did not exit within %s seconds.\n' "$STOP_TIMEOUT"
        printf 'Removing remaining tmux session...\n'

        tmux kill-session -t "$SESSION" 2>/dev/null || true
    fi

    printf '\nStagenet tmux session stopped.\n'

    printf '\nPort status:\n'
    print_port_status monerod "$DAEMON_PORT"
    print_port_status customer "$CUSTOMER_RPC_PORT"
    print_port_status merchant "$MERCHANT_RPC_PORT"

    if port_in_use "$DAEMON_PORT" ||
        port_in_use "$CUSTOMER_RPC_PORT" ||
        port_in_use "$MERCHANT_RPC_PORT"; then
        printf '\nNote: busy ports belong to processes outside the tmux session.\n'
        printf 'Inspect them with:\n'
        printf "  ss -ltnp | grep -E '%s|%s|%s'\n" \
            "$DAEMON_PORT" \
            "$CUSTOMER_RPC_PORT" \
            "$MERCHANT_RPC_PORT"
    fi
}

main "$@"
