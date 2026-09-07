#!/usr/bin/env bash

set -u
set -o pipefail

export MNP="${MNP:-mnp}"
JOBS="${JOBS:-10}"
REQUESTS="${REQUESTS:-100}"
AMOUNT="${AMOUNT:-1000}"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

success=0
failed=0
start_time=0
end_time=0

usage()
{
    cat <<EOF
Usage:
  $0 [rpc|payment|send|all]

Environment:
  MNP       Path to mnp binary       Default: mnp
  JOBS      Parallel processes       Default: 10
  REQUESTS  Number of requests       Default: 100
  AMOUNT    Atomic units per request Default: 1000

Examples:
  JOBS=20 REQUESTS=500 $0 rpc
  JOBS=10 REQUESTS=100 $0 payment
  JOBS=40 REQUESTS=40 AMOUNT=55555 $0 send
  JOBS=20 REQUESTS=500 $0 all

The send test creates real transactions through send_monero.sh.
Use it only with dedicated Stagenet wallets.
EOF
}

run_parallel()
{
    local name="$1"
    shift
    local output_dir
    local i

    output_dir="$(mktemp -d)"

    echo
    echo "=== ${name} ==="
    echo "requests: ${REQUESTS}"
    echo "parallel: ${JOBS}"

    start_time="$(date +%s)"

    for ((i = 1; i <= REQUESTS; i++)); do
        (
            if "$@" >"${output_dir}/${i}.out" 2>"${output_dir}/${i}.err"; then
                printf '0\n' >"${output_dir}/${i}.status"
            else
                printf '%d\n' "$?" >"${output_dir}/${i}.status"
            fi
        ) &

        while (( $(jobs -rp | wc -l) >= JOBS )); do
            sleep 0.05
        done
    done

    wait

    end_time="$(date +%s)"

    success=0
    failed=0

    for ((i = 1; i <= REQUESTS; i++)); do
        if [[ "$(cat "${output_dir}/${i}.status")" == "0" ]]; then
            ((success++))
        else
            ((failed++))

            echo
            echo "Failure ${i}:"
            cat "${output_dir}/${i}.err"
        fi
    done

    echo
    echo "success:  ${success}"
    echo "failed:   ${failed}"
    echo "duration: $((end_time - start_time)) s"

    rm -rf "${output_dir}"

    if ((failed != 0)); then
        return 1
    fi

    return 0
}

rpc_request()
{
    local args value
    for args in balance unlocked bc-height; do
        if [[ "$args" == unlocked ]]; then
            value=$("$MNP" balance --unlocked) || return 1
        else
            value=$("$MNP" "$args") || return 1
        fi
        python3 "$TEST_DIR/test_data.py" number "$value" || return 1
    done
}

send_request()
{
    "$TEST_DIR/create_uri.sh" "$AMOUNT" | "$TEST_DIR/send_monero.sh"
}

rpc_test()
{
    run_parallel "Wallet RPC load test" rpc_request
}

payment_test()
{
    if [[ ! -x "${TEST_DIR}/create_uri.sh" ]]; then
        echo "load_test: ${TEST_DIR}/create_uri.sh is not executable" >&2
        return 1
    fi

    run_parallel "Payment request load test" "$TEST_DIR/create_uri.sh" "$AMOUNT"
}

send_test()
{
    if [[ ! -x "${TEST_DIR}/create_uri.sh" ]]; then
        echo "load_test: ${TEST_DIR}/create_uri.sh is not executable" >&2
        return 1
    fi

    if [[ ! -x "${TEST_DIR}/send_monero.sh" ]]; then
        echo "load_test: ${TEST_DIR}/send_monero.sh is not executable" >&2
        return 1
    fi

    if [[ -z "${RPC_USER:-}" || -z "${RPC_PASSWORD:-}" ]]; then
        echo "load_test: RPC_USER and RPC_PASSWORD are required for send test" >&2
        return 1
    fi

    if [[ "${MNP_TEST_NETWORK:-}" != stagenet ]]; then
        echo "load_test: check both wallet networks and set MNP_TEST_NETWORK=stagenet" >&2
        return 1
    fi

    echo
    echo "WARNING: send test creates real wallet transactions."
    echo "RPC endpoint: ${RPC_HOST:-127.0.0.1}:${RPC_PORT:-30185}"
    echo "amount: ${AMOUNT} atomic units per transaction"

    run_parallel "Transaction submission load test" send_request
}

main()
{
    local mode="${1:-all}"
    local value

    if [[ "$mode" != help && "$mode" != --help && "$mode" != -h ]]; then
        for value in "$JOBS" "$REQUESTS"; do
            if ! [[ "$value" =~ ^[1-9][0-9]{0,6}$ ]]; then
                echo "load_test: JOBS and REQUESTS must be positive integers (maximum 9999999)" >&2
                return 1
            fi
        done
    fi

    case "$mode" in
        rpc)
            rpc_test
            ;;

        payment)
            payment_test
            ;;

        send)
            send_test
            ;;

        all)
            rpc_test || return 1
            payment_test
            ;;

        help|--help|-h)
            usage
            ;;

        *)
            echo "load_test: unknown test '${mode}'" >&2
            usage >&2
            return 1
            ;;
    esac
}

main "$@"
