#!/usr/bin/env bash
# tests/sign_verify_test.sh

set -euo pipefail

MNP="${MNP:-./build/mnp}"
MESSAGE="mnp sign verify test"

fail()
{
    printf '[FAIL] %s\n' "$1" >&2
    exit 1
}

pass()
{
    printf '[PASS] %s\n' "$1"
}

ADDRESS="$("$MNP" payment subaddr 0)" ||
    fail "could not retrieve subaddress"

SIGNATURE="$("$MNP" sign "$MESSAGE")" ||
    fail "could not sign message"

if "$MNP" verify "$ADDRESS" "$SIGNATURE" "$MESSAGE" |
    grep -qx 'true'; then
    pass "argument sign/verify"
else
    fail "argument sign/verify"
fi

PIPE_SIGNATURE="$(printf '%s\n' "$MESSAGE" | "$MNP" sign)" ||
    fail "could not sign stdin"

if "$MNP" verify "$ADDRESS" "$PIPE_SIGNATURE" "$MESSAGE" |
    grep -qx 'true'; then
    pass "single trailing LF handling"
else
    fail "single trailing LF handling"
fi

if printf '%s\n' "$MESSAGE" |
    "$MNP" verify "$ADDRESS" "$SIGNATURE" |
    grep -qx 'true'; then
    pass "stdin verify"
else
    fail "stdin verify"
fi

set +e
OUTPUT="$("$MNP" verify "$ADDRESS" "$SIGNATURE" "wrong message")"
STATUS=$?
set -e

if [ "$OUTPUT" = "false" ] && [ "$STATUS" -ne 0 ]; then
    pass "wrong message rejected"
else
    fail "wrong message rejected"
fi

set +e
"$MNP" verify "invalid-address" "$SIGNATURE" "$MESSAGE" >/dev/null 2>&1
STATUS=$?
set -e

if [ "$STATUS" -ne 0 ]; then
    pass "invalid address rejected"
else
    fail "invalid address rejected"
fi

set +e
"$MNP" verify "$ADDRESS" "invalid-signature" "$MESSAGE" >/dev/null 2>&1
STATUS=$?
set -e

if [ "$STATUS" -ne 0 ]; then
    pass "invalid signature rejected"
else
    fail "invalid signature rejected"
fi

printf '\nAll sign/verify tests passed\n'
