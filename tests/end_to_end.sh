#!/usr/bin/env bash
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_CMD="${DB_CMD:-mariadb}"
RECEIPT_TIMEOUT="${RECEIPT_TIMEOUT:-600}"

if [[ "${MNP_TEST_NETWORK:-}" != stagenet ]]; then
    echo "end_to_end: check both wallet networks and set MNP_TEST_NETWORK=stagenet" >&2
    exit 1
fi
if ! [[ "$RECEIPT_TIMEOUT" =~ ^[1-9][0-9]{0,5}$ ]]; then
    echo "end_to_end: RECEIPT_TIMEOUT must be a positive number of seconds (maximum 999999)" >&2
    exit 1
fi

# The detector and notification consumers must already be running.
uri=$("$TEST_DIR/create_uri.sh" "${1:-55555}")
details=$(printf '%s\n' "$uri" | "$TEST_DIR/split_URI.sh")
read -r payment_id address amount <<< "$details"
payid=$(python3 -c 'import sys; print(int(sys.argv[1], 16))' "$payment_id")
printf '%s\n' "$uri" | "$TEST_DIR/send_monero.sh"

deadline=$((SECONDS + RECEIPT_TIMEOUT))
while (( SECONDS < deadline )); do
    status=$("$DB_CMD" --batch --skip-column-names \
        -e "SELECT STATUS FROM payDB.payments WHERE PAYID = $payid;")
    case "$status" in
        COMPLETED)
            printf 'Payment %s: COMPLETED (%s atomic units)\n' "$payid" "$amount"
            exit 0
            ;;
        REQUEST|WAITING) ;;
        *)
            printf 'Payment %s: unexpected or failed status %s\n' "$payid" "$status" >&2
            exit 1
            ;;
    esac
    sleep 1
done
printf 'Payment %s: receipt timed out after %s seconds\n' "$payid" "$RECEIPT_TIMEOUT" >&2
exit 1
