#!/usr/bin/env python3
"""Exact amount and JSON validation shared by the integration scripts."""

import json
import re
import sys
from decimal import Decimal, InvalidOperation
from urllib.parse import parse_qs, urlsplit


def number(value):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?", value):
        raise ValueError("expected a non-negative integral amount")
    result = Decimal(value)
    if result != result.to_integral_value() or result > 18446744073709551615:
        raise ValueError("amount is outside uint64 range or is fractional")
    return int(result)


def uri_parts(uri):
    parsed = urlsplit(uri)
    if parsed.scheme != "monero" or not re.fullmatch(r"[1-9A-HJ-NP-Za-km-z]{95,106}", parsed.path):
        raise ValueError("invalid Monero URI address")
    values = parse_qs(parsed.query, keep_blank_values=True).get("tx_amount", [])
    if len(values) != 1 or not re.fullmatch(r"[0-9]+(?:\.[0-9]{1,12})?", values[0]):
        raise ValueError("exactly one decimal tx_amount is required")
    whole, _, fraction = values[0].partition(".")
    amount = int(whole) * 10**12 + int(fraction.ljust(12, "0"))
    if not 0 < amount <= 18446744073709551615:
        raise ValueError("transfer amount must be positive and within uint64 range")
    return parsed.path, amount


def main():
    mode = sys.argv[1]
    if mode == "number":
        number(sys.argv[2])
    elif mode == "compare":
        return 0 if number(sys.argv[2]) == number(sys.argv[3]) else 1
    elif mode in ("transfer", "split"):
        address, amount = uri_parts(sys.stdin.read().strip())
        if mode == "transfer":
            account = sys.argv[2]
            if not re.fullmatch(r"[0-9]+", account) or int(account) > 4294967295:
                raise ValueError("invalid account index")
            params = {"destinations": [{"address": address, "amount": amount}],
                      "account_index": int(account), "priority": 0}
            method = "transfer"
        else:
            params = {"integrated_address": address}
            method = "split_integrated_address"
        print(json.dumps({"jsonrpc": "2.0", "id": "0", "method": method, "params": params}))
    elif mode in ("transfer-result", "split-result"):
        reply = json.load(sys.stdin)
        if not isinstance(reply, dict) or reply.get("error") is not None:
            raise ValueError("wallet RPC returned an error")
        result = reply.get("result")
        key, size = ("tx_hash", 64) if mode == "transfer-result" else ("payment_id", 16)
        if not isinstance(result, dict) or not re.fullmatch(r"[0-9a-fA-F]{%d}" % size, str(result.get(key, ""))):
            raise ValueError("wallet RPC result is missing " + key)
        if mode == "transfer-result":
            print(json.dumps(reply))
        else:
            address, amount = uri_parts(sys.argv[2])
            print(result[key], address, amount)
    else:
        raise ValueError("unknown validation mode")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, InvalidOperation, IndexError) as error:
        print("test_data: " + str(error), file=sys.stderr)
        sys.exit(1)
