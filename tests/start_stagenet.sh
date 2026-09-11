#!/usr/bin/env bash
# tests/start_stagenet.sh

set -euo pipefail

SCRIPT_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1
    pwd
)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"

SESSION="${SESSION:-mnp-stagenet}"
WALLET_DIR="${WALLET_DIR:-$HOME/Monero/wallets}"
RUNTIME_DIR="${RUNTIME_DIR:-$SCRIPT_DIR/.runtime}"

MONEROD_BIN="${MONEROD_BIN:-monerod}"
WALLET_RPC_BIN="${WALLET_RPC_BIN:-monero-wallet-rpc}"
MNP_BIN="${MNP_BIN:-$SCRIPT_DIR/.build/mnp}"

DAEMON_HOST="${DAEMON_HOST:-127.0.0.1}"
DAEMON_PORT="${DAEMON_PORT:-38081}"

CUSTOMER_RPC_HOST="${CUSTOMER_RPC_HOST:-127.0.0.1}"
CUSTOMER_RPC_PORT="${CUSTOMER_RPC_PORT:-30185}"
CUSTOMER_RPC_USER="${CUSTOMER_RPC_USER:-customer}"
CUSTOMER_RPC_PASSWORD="${CUSTOMER_RPC_PASSWORD:-customer-test}"

MERCHANT_RPC_HOST="${MERCHANT_RPC_HOST:-127.0.0.1}"
MERCHANT_RPC_PORT="${MERCHANT_RPC_PORT:-38084}"

MNP_CONFIG="${MNP_CONFIG:-$HOME/.mnp.ini}"
CUSTOMER_MNP_CONFIG="$RUNTIME_DIR/customer.mnp.ini"

MERCHANT_RPC_CONFIG="${MERCHANT_RPC_CONFIG:-$HOME/.config/mnp/monero-wallet-rpc.conf}"
MERCHANT_RUNTIME_CONFIG="$RUNTIME_DIR/merchant-wallet-rpc.conf"

START_TIMEOUT="${START_TIMEOUT:-20}"

declare -a WALLETS=()

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

port_in_use()
{
    local port="$1"

    ss -ltnH |
        awk '{print $4}' |
        grep -Eq "[:.]${port}$"
}

wait_for_services()
{
    local deadline=$((SECONDS + START_TIMEOUT))

    while ((SECONDS < deadline)); do
        if port_in_use "$DAEMON_PORT" &&
            port_in_use "$CUSTOMER_RPC_PORT" &&
            port_in_use "$MERCHANT_RPC_PORT"; then
            return 0
        fi

        sleep 0.2
    done

    return 1
}

discover_wallets()
{
    local keys_file
    local wallet_file

    [[ -d "$WALLET_DIR" ]] ||
        die "wallet directory not found: $WALLET_DIR"

    while IFS= read -r -d '' keys_file; do
        wallet_file="${keys_file%.keys}"

        [[ -f "$wallet_file" ]] || continue

        WALLETS+=("$wallet_file")
    done < <(
        find "$WALLET_DIR" \
            -type f \
            -name '*.keys' \
            -print0 |
            sort -z
    )

    ((${#WALLETS[@]} > 0)) ||
        die "no wallets found in $WALLET_DIR"
}

wallet_label()
{
    local wallet="$1"

    printf '%s' "${wallet#"$WALLET_DIR"/}"
}

print_wallets()
{
    local i

    for ((i = 0; i < ${#WALLETS[@]}; i++)); do
        printf '  %d) %s\n' \
            "$((i + 1))" \
            "$(wallet_label "${WALLETS[$i]}")"
    done
}

select_wallet()
{
    local prompt="$1"
    local selection

    printf '\n%s\n' "$prompt" >&2
    print_wallets >&2

    while true; do
        printf '> ' >&2
        IFS= read -r selection

        if [[ "$selection" =~ ^[0-9]+$ ]] &&
            ((selection >= 1 && selection <= ${#WALLETS[@]})); then
            printf '%s\n' "${WALLETS[$((selection - 1))]}"
            return 0
        fi

        printf 'Select a number between 1 and %d.\n' \
            "${#WALLETS[@]}" >&2
    done
}

shell_command()
{
    local argument

    for argument in "$@"; do
        printf '%q ' "$argument"
    done
}

check_mnp_config()
{
    local resolved
    local host
    local port

    [[ -e "$MNP_CONFIG" ]] ||
        die "mnp config not found: $MNP_CONFIG"

    resolved="$(readlink -f "$MNP_CONFIG")"

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

prepare_customer_mnp_config()
{
    local source_config
    local tmp_config

    source_config="$(readlink -f "$MNP_CONFIG")"

    [[ -f "$source_config" ]] ||
        die "mnp config not found: $source_config"

    mkdir -p "$RUNTIME_DIR"

    tmp_config="${CUSTOMER_MNP_CONFIG}.tmp"

    awk \
        -v host="$CUSTOMER_RPC_HOST" \
        -v port="$CUSTOMER_RPC_PORT" \
        -v user="$CUSTOMER_RPC_USER" \
        -v password="$CUSTOMER_RPC_PASSWORD" \
        '
        BEGIN {
            have_host = 0
            have_port = 0
            have_user = 0
            have_password = 0
        }

        /^[[:space:]]*host[[:space:]]*=/ {
            print "host = " host
            have_host = 1
            next
        }

        /^[[:space:]]*port[[:space:]]*=/ {
            print "port = " port
            have_port = 1
            next
        }

        /^[[:space:]]*user[[:space:]]*=/ {
            print "user = " user
            have_user = 1
            next
        }

        /^[[:space:]]*password[[:space:]]*=/ {
            print "password = " password
            have_password = 1
            next
        }

        {
            print
        }

        END {
            if (!have_host) {
                print "host = " host
            }

            if (!have_port) {
                print "port = " port
            }

            if (!have_user) {
                print "user = " user
            }

            if (!have_password) {
                print "password = " password
            }
        }
        ' \
        "$source_config" >"$tmp_config"

    chmod 600 "$tmp_config"
    mv "$tmp_config" "$CUSTOMER_MNP_CONFIG"

    printf 'Customer cfg: %s\n' "$CUSTOMER_MNP_CONFIG"
    printf 'Customer RPC: %s:%s\n' "$CUSTOMER_RPC_HOST" "$CUSTOMER_RPC_PORT"
}

prepare_merchant_config()
{
    local tmp_config

    [[ -f "$MERCHANT_RPC_CONFIG" ]] ||
        die "merchant RPC config not found: $MERCHANT_RPC_CONFIG"

    [[ -x "$MNP_BIN" ]] ||
        die "test mnp binary not found or not executable: $MNP_BIN"

    mkdir -p "$RUNTIME_DIR"

    tmp_config="${MERCHANT_RUNTIME_CONFIG}.tmp"

    awk \
        -v mnp="$MNP_BIN" \
        '
        BEGIN {
            replaced = 0
        }

        /^[[:space:]]*tx-notify[[:space:]]*=/ {
            print "tx-notify=" mnp " --notify-at 1 %s"
            replaced = 1
            next
        }

        {
            print
        }

        END {
            if (!replaced) {
                print "tx-notify=" mnp " --notify-at 1 %s"
            }
        }
        ' \
        "$MERCHANT_RPC_CONFIG" >"$tmp_config"

    mv "$tmp_config" "$MERCHANT_RUNTIME_CONFIG"

    printf 'Merchant cfg: %s\n' "$MERCHANT_RUNTIME_CONFIG"
    printf 'tx-notify:    %s --notify-at 1 %%s\n' "$MNP_BIN"
}

check_environment()
{
    if tmux has-session -t "$SESSION" 2>/dev/null; then
        die "tmux session '$SESSION' already exists"
    fi

    if port_in_use "$DAEMON_PORT"; then
        die "daemon port $DAEMON_PORT is already in use"
    fi

    if port_in_use "$CUSTOMER_RPC_PORT"; then
        die "customer RPC port $CUSTOMER_RPC_PORT is already in use"
    fi

    if port_in_use "$MERCHANT_RPC_PORT"; then
        die "merchant RPC port $MERCHANT_RPC_PORT is already in use"
    fi
}

build_monerod_command()
{
    shell_command \
        "$MONEROD_BIN" \
        --stagenet
}

build_customer_command()
{
    local wallet="$1"
    local -a command=(
        "$WALLET_RPC_BIN"
        --stagenet
        --wallet-file "$wallet"
        --password ""
        --daemon-address "${DAEMON_HOST}:${DAEMON_PORT}"
        --rpc-bind-ip "$CUSTOMER_RPC_HOST"
        --rpc-bind-port "$CUSTOMER_RPC_PORT"
        --rpc-login "${CUSTOMER_RPC_USER}:${CUSTOMER_RPC_PASSWORD}"
    )

    printf 'exec '
    shell_command "${command[@]}"
}

build_merchant_command()
{
    local wallet="$1"
    local -a command=(
        "$WALLET_RPC_BIN"
        --config-file "$MERCHANT_RUNTIME_CONFIG"
        --stagenet
        --wallet-file "$wallet"
        --password ""
        --daemon-address "${DAEMON_HOST}:${DAEMON_PORT}"
        --rpc-bind-ip "$MERCHANT_RPC_HOST"
        --rpc-bind-port "$MERCHANT_RPC_PORT"
    )

    printf 'exec '
    shell_command "${command[@]}"
}

print_summary()
{
    local customer_wallet="$1"
    local merchant_wallet="$2"

    printf '\nStagenet environment\n'
    printf '%s\n' '--------------------'
    printf 'Session:      %s\n' "$SESSION"
    printf 'Customer:     %s\n' "$(wallet_label "$customer_wallet")"
    printf 'Customer RPC: %s:%s\n' \
        "$CUSTOMER_RPC_HOST" \
        "$CUSTOMER_RPC_PORT"
    printf 'Customer cfg: %s\n' "$CUSTOMER_MNP_CONFIG"
    printf 'Merchant:     %s\n' "$(wallet_label "$merchant_wallet")"
    printf 'Merchant RPC: %s:%s\n' \
        "$MERCHANT_RPC_HOST" \
        "$MERCHANT_RPC_PORT"
    printf 'Daemon:       %s:%s\n' \
        "$DAEMON_HOST" \
        "$DAEMON_PORT"
    printf 'Test mnp:     %s\n' "$MNP_BIN"
}

start_tmux()
{
    local customer_wallet="$1"
    local merchant_wallet="$2"
    local monerod_command
    local customer_command
    local merchant_command

    monerod_command="$(build_monerod_command)"
    customer_command="$(build_customer_command "$customer_wallet")"
    merchant_command="$(build_merchant_command "$merchant_wallet")"

    tmux new-session \
        -d \
        -s "$SESSION" \
        -n monerod \
        "$monerod_command"

    tmux set-window-option \
        -t "$SESSION" \
        -g \
        remain-on-exit on

    tmux new-window \
        -t "$SESSION" \
        -n customer \
        "$customer_command"

    tmux new-window \
        -t "$SESSION" \
        -n merchant \
        "$merchant_command"

    tmux select-window -t "$SESSION:monerod"
}

show_pane()
{
    local window="$1"

    printf '\n--- %s ---\n' "$window"

    tmux capture-pane \
        -p \
        -t "$SESSION:$window" 2>/dev/null ||
        true
}

verify_services()
{
    local failed=0

    printf '\nChecking services...\n'

    wait_for_services || true

    if port_in_use "$DAEMON_PORT"; then
        printf '  [OK] monerod        %s:%s\n' \
            "$DAEMON_HOST" \
            "$DAEMON_PORT"
    else
        printf '  [FAIL] monerod      %s:%s\n' \
            "$DAEMON_HOST" \
            "$DAEMON_PORT"
        failed=1
    fi

    if port_in_use "$CUSTOMER_RPC_PORT"; then
        printf '  [OK] customer RPC   %s:%s\n' \
            "$CUSTOMER_RPC_HOST" \
            "$CUSTOMER_RPC_PORT"
    else
        printf '  [FAIL] customer RPC %s:%s\n' \
            "$CUSTOMER_RPC_HOST" \
            "$CUSTOMER_RPC_PORT"
        failed=1
    fi

    if port_in_use "$MERCHANT_RPC_PORT"; then
        printf '  [OK] merchant RPC   %s:%s\n' \
            "$MERCHANT_RPC_HOST" \
            "$MERCHANT_RPC_PORT"
    else
        printf '  [FAIL] merchant RPC %s:%s\n' \
            "$MERCHANT_RPC_HOST" \
            "$MERCHANT_RPC_PORT"
        failed=1
    fi

    if ((failed != 0)); then
        printf '\nOne or more services failed to start.\n'
        printf 'The tmux windows were intentionally kept open.\n'

        show_pane monerod
        show_pane customer
        show_pane merchant

        printf '\nInspect manually with:\n'
        printf '  tmux attach -t %s\n' "$SESSION"

        return 1
    fi

    return 0
}

verify_tx_notify()
{
    local configured

    configured="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*tx-notify[[:space:]]*$/ {
                sub(/^[^=]*=/, "")
                print
            }
        ' "$MERCHANT_RUNTIME_CONFIG"
    )"

    [[ "$configured" == "$MNP_BIN --notify-at 1 %s" ]] ||
        die "runtime tx-notify is incorrect: $configured"

    printf '  [OK] tx-notify      %s\n' "$configured"
}

verify_customer_mnp_config()
{
    local host
    local port
    local user

    host="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*host[[:space:]]*$/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
                print $2
            }
        ' "$CUSTOMER_MNP_CONFIG"
    )"

    port="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*port[[:space:]]*$/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
                print $2
            }
        ' "$CUSTOMER_MNP_CONFIG"
    )"

    user="$(
        awk -F '=' '
            $1 ~ /^[[:space:]]*user[[:space:]]*$/ {
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
                print $2
            }
        ' "$CUSTOMER_MNP_CONFIG"
    )"

    [[ "$host" == "$CUSTOMER_RPC_HOST" ]] ||
        die "customer mnp config has incorrect host"

    [[ "$port" == "$CUSTOMER_RPC_PORT" ]] ||
        die "customer mnp config has incorrect port"

    [[ "$user" == "$CUSTOMER_RPC_USER" ]] ||
        die "customer mnp config has incorrect user"

    printf '  [OK] customer cfg   %s:%s\n' "$host" "$port"
}

main()
{
    local customer_wallet
    local merchant_wallet
    local answer

    require_command tmux
    require_command find
    require_command sort
    require_command ss
    require_command awk
    require_command grep
    require_command readlink
    require_command "$MONEROD_BIN"
    require_command "$WALLET_RPC_BIN"

    discover_wallets

    customer_wallet="$(
        select_wallet \
            "Select customer wallet (FULL KEY / spend-capable):"
    )"

    merchant_wallet="$(
        select_wallet \
            "Select merchant wallet (VIEW-ONLY):"
    )"

    if [[ "$customer_wallet" == "$merchant_wallet" ]]; then
        die "customer and merchant wallets must be different"
    fi

    check_mnp_config
    check_environment

    prepare_customer_mnp_config
    prepare_merchant_config

    print_summary \
        "$customer_wallet" \
        "$merchant_wallet"

    printf '\nStart this configuration? [y/N] '
    IFS= read -r answer

    case "$answer" in
    y|Y|yes|YES)
        ;;
    *)
        printf 'Cancelled.\n'
        exit 0
        ;;
    esac

    start_tmux \
        "$customer_wallet" \
        "$merchant_wallet"

    printf '\nStarted tmux session: %s\n' "$SESSION"

    if ! verify_services; then
        exit 1
    fi

    verify_tx_notify
    verify_customer_mnp_config

    printf '\nAll services are listening.\n'

    printf '\nCustomer mnp tests:\n'
    printf '  %q --config %q balance\n' \
        "$MNP_BIN" \
        "$CUSTOMER_MNP_CONFIG"
    printf '  %q --config %q transfer URI\n' \
        "$MNP_BIN" \
        "$CUSTOMER_MNP_CONFIG"

    printf '\nAttach:\n'
    printf '  tmux attach -t %s\n' "$SESSION"

    printf '\nWindows:\n'
    printf '  Ctrl-b 0   monerod\n'
    printf '  Ctrl-b 1   customer RPC\n'
    printf '  Ctrl-b 2   merchant RPC\n'

    printf '\nDetach without stopping services:\n'
    printf '  Ctrl-b d\n'

    printf '\nStop the environment:\n'
    printf '  %s/stop_stagenet.sh\n' "$SCRIPT_DIR"
}

main "$@"
