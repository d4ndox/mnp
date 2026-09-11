#!/usr/bin/env bash

set -euo pipefail

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/mnp"
MNP_CONFIG="${CONFIG_DIR}/mnp.ini"
MONEROD_CONFIG="${CONFIG_DIR}/monerod.conf"
WALLET_RPC_CONFIG="${CONFIG_DIR}/monero-wallet-rpc.conf"
RPC_PASSWORD_FILE="${CONFIG_DIR}/rpc.password"
WALLET_RPC_PIDFILE="${CONFIG_DIR}/monero-wallet-rpc.pid"

wallet_rpc_port=""
wallet_rpc_pid=""
wallet_password_file=""
wallet_password_args=()
prompt_password=0

usage()
{
    cat <<EOF
Usage:
  $0 [-p]

Options:
  -p, --prompt-password  Interactive mode for password prompts and hardware wallets
  -h, --help             Show this help

Starts monerod and monero-wallet-rpc using:

  ${CONFIG_DIR}
EOF
}

ok()
{
    printf '[OK] %s\n' "$1"
}

error()
{
    printf '[ERROR] %s\n' "$1" >&2
}

print_monero_install_help()
{
    cat >&2 <<'EOF'

Monero CLI is required.

Download the official Monero CLI from:

  https://www.getmonero.org/downloads/

After extracting the archive, install monerod and monero-wallet-rpc
into /usr/local/bin:

  sudo install -m 755 /path/to/monero/monerod \
      /usr/local/bin/monerod

  sudo install -m 755 /path/to/monero/monero-wallet-rpc \
      /usr/local/bin/monero-wallet-rpc

Verify afterwards:

  command -v monerod
  command -v monero-wallet-rpc
EOF
}

require_monero()
{
    local missing=0

    if ! command -v monerod >/dev/null 2>&1; then
        error "monerod not found"
        missing=1
    fi

    if ! command -v monero-wallet-rpc >/dev/null 2>&1; then
        error "monero-wallet-rpc not found"
        missing=1
    fi

    if ((missing)); then
        print_monero_install_help
        return 1
    fi

    ok "monerod: $(command -v monerod)"
    ok "monero-wallet-rpc: $(command -v monero-wallet-rpc)"
}

require_command()
{
    local command_name="$1"

    if ! command -v "$command_name" >/dev/null 2>&1; then
        error "Required command '${command_name}' not found"
        return 1
    fi

    ok "${command_name}: $(command -v "$command_name")"
}

require_file()
{
    local path="$1"

    if [ ! -r "$path" ]; then
        error "Required file '${path}' not found or not readable"
        return 1
    fi

    ok "$path"
}

check_dependencies()
{
    echo "Checking runtime dependencies..."

    require_monero
    require_command mnp
    require_command ss

    echo
}

configure_wallet_password()
{
    if ((prompt_password)); then
        wallet_password_args=(--prompt-for-password)
        ok "Wallet password: prompt"
        return
    fi

    wallet_password_file="$(
        awk -F= '
            /^[[:space:]]*password-file[[:space:]]*=/ {
                sub(/^[[:space:]]*/, "", $2)
                sub(/[[:space:]]*$/, "", $2)
                print $2
                exit
            }
        ' "$WALLET_RPC_CONFIG"
    )"

    if [ -n "$wallet_password_file" ]; then
        require_file "$wallet_password_file"
        ok "Wallet password file: ${wallet_password_file}"
        return
    fi

    wallet_password_args=(--password "")
    ok "Wallet password: none"
}

check_configuration()
{
    echo "Checking configuration..."

    require_file "$MNP_CONFIG"
    require_file "$MONEROD_CONFIG"
    require_file "$WALLET_RPC_CONFIG"
    require_file "$RPC_PASSWORD_FILE"

    configure_wallet_password

    echo
}

read_wallet_rpc_port()
{
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

wait_for_wallet_rpc()
{
    local i

    for ((i = 0; i < 100; i++)); do
        if get_listener_pid "$wallet_rpc_port" >/dev/null 2>&1; then
            return 0
        fi

        sleep 0.1
    done

    return 1
}

stop_existing_wallet_rpc()
{
    local pid="$1"

    echo "Stopping monero-wallet-rpc (PID ${pid})..."

    kill "$pid"

    if ! wait_for_process "$pid"; then
        error "monero-wallet-rpc did not stop"
        return 1
    fi

    rm -f "$WALLET_RPC_PIDFILE"

    ok "monero-wallet-rpc stopped"
}

check_wallet_rpc_port()
{
    local status
    local process_name
    local answer

    set +e
    wallet_rpc_pid="$(get_listener_pid "$wallet_rpc_port")"
    status=$?
    set -e

    case "$status" in
        1)
            return
            ;;
        2)
            error "Port ${wallet_rpc_port} is in use but the process could not be identified"
            return 1
            ;;
    esac

    process_name="$(get_process_name "$wallet_rpc_pid")"

    if [ "$process_name" != "monero-wallet-rpc" ]; then
        error "Port ${wallet_rpc_port} is used by '${process_name}' (PID ${wallet_rpc_pid})"
        return 1
    fi

    echo
    echo "monero-wallet-rpc is already listening on port ${wallet_rpc_port}."
    echo "PID: ${wallet_rpc_pid}"
    echo

    read -r -p "Restart monero-wallet-rpc? [y/N] " answer

    case "$answer" in
        y|Y|yes|YES)
            stop_existing_wallet_rpc "$wallet_rpc_pid"
            ;;
        *)
            ok "Keeping existing monero-wallet-rpc running"
            exit 0
            ;;
    esac
}

start_monerod()
{
    if pgrep -x monerod >/dev/null 2>&1; then
        ok "monerod is already running"
        return
    fi

    echo "Starting monerod..."

    monerod \
        --config-file "$MONEROD_CONFIG" \
        --detach

    sleep 1

    if ! pgrep -x monerod >/dev/null 2>&1; then
        error "monerod failed to start"
        return 1
    fi

    ok "monerod started"
}

start_wallet_rpc_interactive()
{
    rm -f "$WALLET_RPC_PIDFILE"

    echo
    echo "Starting monero-wallet-rpc interactively on port ${wallet_rpc_port}..."
    echo "Keep this terminal open while monero-wallet-rpc is running."
    echo

    exec monero-wallet-rpc \
        --config-file "$WALLET_RPC_CONFIG" \
        --prompt-for-password
}

start_wallet_rpc_detached()
{
    rm -f "$WALLET_RPC_PIDFILE"

    echo "Starting monero-wallet-rpc on port ${wallet_rpc_port}..."

    monero-wallet-rpc \
        --config-file "$WALLET_RPC_CONFIG" \
        --detach \
        --pidfile "$WALLET_RPC_PIDFILE" \
        "${wallet_password_args[@]}"

    if ! wait_for_wallet_rpc; then
        error "monero-wallet-rpc failed to start on port ${wallet_rpc_port}"
        return 1
    fi

    ok "monero-wallet-rpc started"
    ok "Wallet RPC: 127.0.0.1:${wallet_rpc_port}"

    if [ -r "$WALLET_RPC_PIDFILE" ]; then
        ok "PID: $(cat "$WALLET_RPC_PIDFILE")"
    fi
}

start_wallet_rpc()
{
    if ((prompt_password)); then
        start_wallet_rpc_interactive
        return
    fi

    start_wallet_rpc_detached
}

check_mnp_config()
{
    local config="$HOME/.mnp.ini"
    local resolved
    local host
    local port

    [[ -e "$config" ]] ||
        die "mnp config not found: $config"

    resolved="$(readlink -f "$config")"

    host="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*host[[:space:]]*$/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
                print $2
            }
        ' "$resolved"
    )"

    port="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*port[[:space:]]*$/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
                print $2
            }
        ' "$resolved"
    )"

    [[ "$host" == "$MERCHANT_RPC_HOST" ]] ||
        die "mnp config host is '$host', expected '$MERCHANT_RPC_HOST'"

    [[ "$port" == "$MERCHANT_RPC_PORT" ]] ||
        die "mnp config port is '$port', expected '$MERCHANT_RPC_PORT'"

    printf 'mnp config:    %s:%s\n' "$host" "$port"
}

main()
{
    while (($# > 0)); do
        case "$1" in
            -p|--prompt-password)
                prompt_password=1
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

        shift
    done

    check_mnp_config
    check_dependencies
    check_configuration
    read_wallet_rpc_port
    check_wallet_rpc_port
    start_monerod
    start_wallet_rpc
}

main "$@"
