#!/usr/bin/env bash

set -euo pipefail
umask 077

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/mnp"
MNP_CONFIG="${CONFIG_DIR}/mnp.ini"
MONEROD_CONFIG="${CONFIG_DIR}/monerod.conf"
WALLET_RPC_CONFIG="${CONFIG_DIR}/monero-wallet-rpc.conf"
WALLET_PASSWORD_FILE="${CONFIG_DIR}/wallet.password"
RPC_PASSWORD_FILE="${CONFIG_DIR}/rpc.password"
LEGACY_MNP_CONFIG="${HOME}/.mnp.ini"
DEFAULT_WALLET_DIR="${HOME}/Monero/wallets"
WORKDIR="/tmp/mywallet"

wallet_path=""
wallet_password=""
wallet_has_password=0
network=""
daemon_port=""
wallet_rpc_port=""
rpc_password=""
mnp_bin="mnp"
missing_runtime=0
confirmations=0
tx_notify_args="--notify-at 1"

usage()
{
    cat <<EOF
Usage:
  $0

Creates the mnp configuration in:

  ${CONFIG_DIR}

No Monero process is started.
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

check_optional_command()
{
    local command_name="$1"

    if command -v "$command_name" >/dev/null 2>&1; then
        ok "${command_name}: $(command -v "$command_name")"
        return
    fi

    warn "${command_name} is not installed or not in PATH"
    missing_runtime=1
}

check_dependencies()
{
    echo "Checking runtime dependencies..."

    check_optional_command monerod
    check_optional_command monero-wallet-rpc

    if command -v mnp >/dev/null 2>&1; then
        mnp_bin="$(command -v mnp)"
        ok "mnp: ${mnp_bin}"
    else
        warn "mnp is not installed or not in PATH"
        missing_runtime=1
    fi

    echo
}

create_config_dir()
{
    mkdir -p "$CONFIG_DIR"
    chmod 700 "$CONFIG_DIR"

    ok "Configuration directory: ${CONFIG_DIR}"
    echo
}

find_wallets()
{
    local wallet
    local -n result="$1"

    result=()

    if [ ! -d "$DEFAULT_WALLET_DIR" ]; then
        return
    fi

    while IFS= read -r -d '' wallet; do
        case "$wallet" in
            *.keys|*.address.txt)
                continue
                ;;
        esac

        if [ -f "${wallet}.keys" ]; then
            result+=("$wallet")
        fi
    done < <(
        find "$DEFAULT_WALLET_DIR" \
            -maxdepth 2 \
            -type f \
            -print0 2>/dev/null |
            sort -z
    )
}

read_wallet_path()
{
    while true; do
        read -r -e -p "Wallet path: " wallet_path

        wallet_path="${wallet_path/#\~/$HOME}"

        if [ -f "$wallet_path" ] &&
           [ -f "${wallet_path}.keys" ]; then
            return
        fi

        warn "Wallet or '${wallet_path}.keys' not found"
    done
}

select_wallet()
{
    local -a wallets
    local selection
    local i

    find_wallets wallets

    echo "Wallet"
    echo

    if ((${#wallets[@]} == 0)); then
        warn "No wallets found in ${DEFAULT_WALLET_DIR}"
        read_wallet_path
    else
        for ((i = 0; i < ${#wallets[@]}; i++)); do
            printf '  %d) %s\n' "$((i + 1))" "${wallets[$i]}"
        done

        printf '  %d) Enter another path\n' "$((${#wallets[@]} + 1))"
        echo

        while true; do
            read -r -p "Select wallet: " selection

            if ! [[ "$selection" =~ ^[0-9]+$ ]]; then
                warn "Please enter a number"
                continue
            fi

            if ((selection >= 1 && selection <= ${#wallets[@]})); then
                wallet_path="${wallets[$((selection - 1))]}"
                break
            fi

            if ((selection == ${#wallets[@]} + 1)); then
                read_wallet_path
                break
            fi

            warn "Invalid selection"
        done
    fi

    wallet_path="$(realpath "$wallet_path")"

    ok "Wallet: ${wallet_path}"
    echo
}

select_network()
{
    local selection

    echo "Network"
    echo
    echo "  1) Mainnet"
    echo "  2) Stagenet"
    echo "  3) Testnet"
    echo

    while true; do
        read -r -p "Select network [1]: " selection
        selection="${selection:-1}"

        case "$selection" in
            1)
                network="mainnet"
                daemon_port="18081"
                wallet_rpc_port="18084"
                break
                ;;
            2)
                network="stagenet"
                daemon_port="38081"
                wallet_rpc_port="38084"
                break
                ;;
            3)
                network="testnet"
                daemon_port="28081"
                wallet_rpc_port="28084"
                break
                ;;
            *)
                warn "Invalid selection"
                ;;
        esac
    done

    ok "Network: ${network}"
    ok "Daemon RPC: 127.0.0.1:${daemon_port}"
    ok "Wallet RPC: 127.0.0.1:${wallet_rpc_port}"
    echo
}

read_confirmations()
{
    local input

    echo "Transaction notification"
    echo

    while true; do
        read -r -p "Confirmations [0]: " input
        input="${input:-0}"

        if [[ "$input" =~ ^[0-9]+$ ]]; then
            confirmations="$input"
            break
        fi

        warn "Confirmations must be a non-negative integer"
    done

    if ((confirmations == 0)); then
        tx_notify_args="--notify-at 1"
        ok "Transaction notification: txpool"
    else
        tx_notify_args="--notify-at 2 --confirmation ${confirmations}"
        ok "Transaction notification: ${confirmations} confirmation(s)"
    fi

    echo
}

read_wallet_password()
{
    local answer
    local confirmation

    echo "Wallet password"
    echo

    read -r -p "Is the wallet password protected? [y/N] " answer

    case "$answer" in
        y|Y|yes|YES)
            wallet_has_password=1
            ;;
        *)
            wallet_has_password=0
            wallet_password=""
            ok "Wallet password: none"
            echo
            return
            ;;
    esac

    while true; do
        IFS= read -r -s -p "Wallet password: " wallet_password
        echo
        IFS= read -r -s -p "Confirm password: " confirmation
        echo

        if [ "$wallet_password" = "$confirmation" ]; then
            break
        fi

        warn "Passwords do not match"
    done

    ok "Wallet password configured"
    echo
}

generate_rpc_password()
{
    rpc_password="$(
        od -An -N24 -tx1 /dev/urandom |
            tr -d ' \n'
    )"

    if [ "${#rpc_password}" -ne 48 ]; then
        error "Could not generate RPC password"
        exit 1
    fi
}

write_wallet_password()
{
    if ((wallet_has_password)); then
        printf '%s\n' "$wallet_password" >"$WALLET_PASSWORD_FILE"
        chmod 600 "$WALLET_PASSWORD_FILE"
        return
    fi

    rm -f "$WALLET_PASSWORD_FILE"
}

write_rpc_password()
{
    printf '%s\n' "$rpc_password" >"$RPC_PASSWORD_FILE"
    chmod 600 "$RPC_PASSWORD_FILE"
}

write_monerod_config()
{
    {
        case "$network" in
            stagenet)
                echo "stagenet=1"
                ;;
            testnet)
                echo "testnet=1"
                ;;
        esac

        echo "rpc-bind-ip=127.0.0.1"
    } >"$MONEROD_CONFIG"

    chmod 600 "$MONEROD_CONFIG"
}

write_wallet_rpc_config()
{
    {
        case "$network" in
            stagenet)
                echo "stagenet=1"
                ;;
            testnet)
                echo "testnet=1"
                ;;
        esac

        printf 'wallet-file=%s\n' "$wallet_path"

        if ((wallet_has_password)); then
            printf 'password-file=%s\n' "$WALLET_PASSWORD_FILE"
        fi

        echo "rpc-bind-ip=127.0.0.1"
        printf 'rpc-bind-port=%s\n' "$wallet_rpc_port"
        printf 'rpc-login=mnp:%s\n' "$rpc_password"
        printf 'daemon-address=127.0.0.1:%s\n' "$daemon_port"
        echo "trusted-daemon=1"
        printf 'tx-notify=%s %s %%s\n' \
            "$mnp_bin" \
            "$tx_notify_args"
    } >"$WALLET_RPC_CONFIG"

    chmod 600 "$WALLET_RPC_CONFIG"
}

write_mnp_config()
{
    cat >"$MNP_CONFIG" <<EOF
[rpc]
user = mnp
password = ${rpc_password}
host = 127.0.0.1
port = ${wallet_rpc_port}

[mnp]
verbose = 0
account = 0

[cfg]
workdir = ${WORKDIR}
mode = rwx------
pipe = rw-------
EOF

    chmod 600 "$MNP_CONFIG"
}

create_legacy_config_link()
{
    local answer

    if [ -L "$LEGACY_MNP_CONFIG" ]; then
        if [ "$(readlink -f "$LEGACY_MNP_CONFIG")" = "$(readlink -f "$MNP_CONFIG")" ]; then
            ok "Legacy config link already exists"
            return
        fi

        ln -sfn "$MNP_CONFIG" "$LEGACY_MNP_CONFIG"
        ok "Updated legacy config link: ${LEGACY_MNP_CONFIG}"
        return
    fi

    if [ -e "$LEGACY_MNP_CONFIG" ]; then
        warn "Existing configuration found: ${LEGACY_MNP_CONFIG}"

        read -r -p "Back up and replace it with a symlink? [y/N] " answer

        case "$answer" in
            y|Y|yes|YES)
                mv \
                    "$LEGACY_MNP_CONFIG" \
                    "${LEGACY_MNP_CONFIG}.backup"

                ln -s "$MNP_CONFIG" "$LEGACY_MNP_CONFIG"

                ok "Previous config saved as ${LEGACY_MNP_CONFIG}.backup"
                ok "Legacy config link created: ${LEGACY_MNP_CONFIG}"
                ;;
            *)
                warn "Keeping existing ${LEGACY_MNP_CONFIG}"
                ;;
        esac

        return
    fi

    ln -s "$MNP_CONFIG" "$LEGACY_MNP_CONFIG"
    ok "Legacy config link created: ${LEGACY_MNP_CONFIG}"
}

print_summary()
{
    echo
    echo "Configuration complete."
    echo
    echo "  Wallet:        ${wallet_path}"
    echo "  Network:       ${network}"
    echo "  Daemon RPC:    127.0.0.1:${daemon_port}"
    echo "  Wallet RPC:    127.0.0.1:${wallet_rpc_port}"

    if ((confirmations == 0)); then
        echo "  Notification:  txpool"
    else
        echo "  Confirmations: ${confirmations}"
    fi

    echo "  Config:        ${CONFIG_DIR}"

    if ((wallet_has_password)); then
        echo "  Password:      ${WALLET_PASSWORD_FILE}"
    else
        echo "  Password:      none"
    fi

    echo
    echo "  Legacy:        ${LEGACY_MNP_CONFIG} -> ${MNP_CONFIG}"
    echo

    if ((missing_runtime)); then
        warn "Some runtime dependencies are missing"
        echo "       start.sh will perform the strict runtime checks." >&2
    fi

    echo "No Monero process has been started."
}

main()
{
    case "${1:-}" in
        "")
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

    check_dependencies
    create_config_dir
    select_wallet
    select_network
    read_confirmations
    read_wallet_password
    generate_rpc_password
    write_wallet_password
    write_rpc_password
    write_monerod_config
    write_wallet_rpc_config
    write_mnp_config
    create_legacy_config_link
    print_summary
}

main "$@"
