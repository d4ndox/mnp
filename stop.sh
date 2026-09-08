#!/usr/bin/env bash

set -euo pipefail

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/mnp"
WALLET_RPC_CONFIG="${CONFIG_DIR}/monero-wallet-rpc.conf"
WALLET_RPC_PIDFILE="${CONFIG_DIR}/monero-wallet-rpc.pid"

wallet_rpc_port=""

usage()
{
    cat <<EOF
Usage:
  $0 [--all]

Options:
  --all       Stop monero-wallet-rpc and monerod
  -h, --help  Show this help

Without --all only the configured monero-wallet-rpc is stopped.
EOF
}

ok()
{
    printf '[OK] %s\n' "$1"
}

warn()
{
    printf '[WARN] %s\n' "$1" >&2
}

error()
{
    printf '[ERROR] %s\n' "$1" >&2
}

require_command()
{
    local command_name="$1"

    if ! command -v "$command_name" >/dev/null 2>&1; then
        error "Required command '${command_name}' not found"
        return 1
    fi
}

read_wallet_rpc_port()
{
    if [ ! -r "$WALLET_RPC_CONFIG" ]; then
        error "Required file '${WALLET_RPC_CONFIG}' not found or not readable"
        return 1
    fi

    wallet_rpc_port="$(
        awk -F= '
            /^[[:space:]]*rpc-bind-port[[:space:]]*=/ {
                gsub(/[[:space:]]/, "", $2)
                print $2
                exit
            }
        ' "$WALLET_RPC_CONFIG"
    )"

    if ! [[ "$wallet_rpc_port" =~ ^[0-9]+$ ]] ||
       ((wallet_rpc_port < 1 || wallet_rpc_port > 65535)); then
        error "Invalid or missing rpc-bind-port in '${WALLET_RPC_CONFIG}'"
        return 1
    fi
}

get_process_name()
{
    local pid="$1"
    local executable

    executable="$(readlink -f "/proc/${pid}/exe" 2>/dev/null || true)"

    if [ -n "$executable" ]; then
        basename "$executable"
        return
    fi

    printf 'unknown\n'
}

wait_for_process()
{
    local pid="$1"
    local i

    for ((i = 0; i < 50; i++)); do
        if ! kill -0 "$pid" 2>/dev/null; then
            return 0
        fi

        sleep 0.1
    done

    return 1
}

stop_pid()
{
    local pid="$1"
    local process_name

    if ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi

    process_name="$(get_process_name "$pid")"

    if [ "$process_name" != "monero-wallet-rpc" ]; then
        error "PID ${pid} belongs to '${process_name}', not monero-wallet-rpc"
        return 2
    fi

    echo "Stopping monero-wallet-rpc (PID ${pid})..."

    kill "$pid"

    if ! wait_for_process "$pid"; then
        error "monero-wallet-rpc did not stop"
        return 1
    fi

    rm -f "$WALLET_RPC_PIDFILE"

    ok "monero-wallet-rpc stopped"
}

stop_wallet_rpc_from_pidfile()
{
    local pid
    local status

    if [ ! -r "$WALLET_RPC_PIDFILE" ]; then
        return 1
    fi

    pid="$(cat "$WALLET_RPC_PIDFILE")"

    if ! [[ "$pid" =~ ^[0-9]+$ ]]; then
        warn "Invalid PID file: ${WALLET_RPC_PIDFILE}"
        rm -f "$WALLET_RPC_PIDFILE"
        return 1
    fi

    set +e
    stop_pid "$pid"
    status=$?
    set -e

    case "$status" in
        0)
            return 0
            ;;
        1)
            warn "PID file is stale"
            rm -f "$WALLET_RPC_PIDFILE"
            return 1
            ;;
        2)
            return 2
            ;;
    esac
}

get_listener_pid()
{
    local port="$1"
    local listener

    listener="$(
        ss -H -ltnp "sport = :${port}" 2>/dev/null |
            head -n1
    )"

    if [ -z "$listener" ]; then
        return 1
    fi

    if [[ "$listener" =~ pid=([0-9]+) ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
        return 0
    fi

    return 2
}

stop_wallet_rpc_from_port()
{
    local pid
    local status
    local process_name

    set +e
    pid="$(get_listener_pid "$wallet_rpc_port")"
    status=$?
    set -e

    case "$status" in
        1)
            ok "monero-wallet-rpc is not running on port ${wallet_rpc_port}"
            return 0
            ;;
        2)
            error "Port ${wallet_rpc_port} is in use but the process could not be identified"
            return 1
            ;;
    esac

    process_name="$(get_process_name "$pid")"

    if [ "$process_name" != "monero-wallet-rpc" ]; then
        error "Port ${wallet_rpc_port} is used by '${process_name}' (PID ${pid})"
        return 1
    fi

    stop_pid "$pid"
}

stop_wallet_rpc()
{
    local status

    set +e
    stop_wallet_rpc_from_pidfile
    status=$?
    set -e

    case "$status" in
        0)
            return
            ;;
        2)
            return 1
            ;;
    esac

    stop_wallet_rpc_from_port
}

stop_monerod()
{
    local -a pids
    local pid

    mapfile -t pids < <(
        pgrep -u "$UID" -x monerod 2>/dev/null || true
    )

    if ((${#pids[@]} == 0)); then
        ok "monerod is not running"
        return
    fi

    echo "Stopping monerod..."

    kill "${pids[@]}"

    for pid in "${pids[@]}"; do
        if ! wait_for_process "$pid"; then
            error "monerod PID ${pid} did not stop"
            return 1
        fi
    done

    ok "monerod stopped"
}

main()
{
    local stop_all=0

    case "${1:-}" in
        "")
            ;;
        --all)
            stop_all=1
            ;;
        help|--help|-h)
            usage
            return
            ;;
        *)
            usage >&2
            return 1
            ;;
    esac

    if (($# > 1)); then
        usage >&2
        return 1
    fi

    require_command ss
    read_wallet_rpc_port
    stop_wallet_rpc

    if ((stop_all)); then
        stop_monerod
    fi
}

main "$@"
