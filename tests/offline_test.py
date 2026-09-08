#!/usr/bin/env python3
"""CLI and harness regression tests; no wallets, database, or transfers needed."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

TESTS = Path(__file__).resolve().parent
MNP = os.environ.get("MNP", str(TESTS / ".build" / "mnp"))


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix=".tmp-", dir=TESTS)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ["PATH"],
                        RPC_USER="offline", RPC_PASSWORD="offline", MNP_TEST_NETWORK="stagenet")

    def executable(self, name, body):
        path = self.root / name
        path.write_text("#!/usr/bin/env python3\n" + body)
        path.chmod(0o700)
        return str(path)

    def run_script(self, name, *args, input="", **env):
        return subprocess.run(["bash", str(TESTS / name), *args], input=input,
                              text=True, capture_output=True, timeout=10,
                              env=dict(self.env, **env))

    def test_sender_validates_rpc_result_and_exact_amount(self):
        self.executable("curl", "import os, sys, json\n"
                        "args=sys.argv; request=json.loads(args[args.index('--data')+1])\n"
                        "assert request['params']['destinations'][0]['amount'] == 9007199254740993\n"
                        "print(os.environ['MOCK_REPLY'])\n")
        uri = "monero:" + "5" * 106 + "?tx_amount=9007.199254740993\n"
        good = json.dumps({"result": {"tx_hash": "a" * 64}})
        for reply, expected in [(good, 0), ('{"error":{"code":-1}}', 1),
                                ('{"result":{}}', 1), ('not json', 1)]:
            with self.subTest(reply=reply):
                result = self.run_script("send_monero.sh", input=uri, MOCK_REPLY=reply)
                self.assertEqual(result.returncode, expected, result.stderr)
        result = self.run_script("send_monero.sh", input=uri, MNP_TEST_NETWORK="")
        self.assertNotEqual(result.returncode, 0)

    def test_sender_rejects_bad_amount_before_rpc(self):
        self.executable("curl", "raise RuntimeError('RPC must not be called')\n")
        for amount in ["0", "-1", "0.0000000000001", "1&tx_amount=2", "18446745"]:
            result = self.run_script("send_monero.sh", input="monero:" + "5" * 106 + "?tx_amount=" + amount)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("RPC must not be called", result.stderr)

    def test_create_uri_honors_binary_and_marks_failure(self):
        log = self.root / "sql"
        db = self.executable("db", "import sys, os\n"
                             "with open(os.environ['SQL_LOG'], 'a') as f: f.write(sys.argv[-1]+'\\n')\n"
                             "if 'INSERT' in sys.argv[-1]: print(42)\n")
        binary = self.executable("mnp with spaces", "import sys, os\n"
                                 "assert sys.argv[1:] == ['payment', '--amount', '55555']\n"
                                 "assert sys.stdin.read().strip() == '000000000000002a'\n"
                                 "if os.environ.get('FAIL_MNP'): sys.exit(1)\n"
                                 "print('monero:fixture')\n")
        result = self.run_script("create_uri.sh", "00055555", MNP=binary, DB_CMD=db, SQL_LOG=str(log))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "monero:fixture\n")
        result = self.run_script("create_uri.sh", "55555", MNP=binary, DB_CMD=db,
                                 SQL_LOG=str(log), FAIL_MNP="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("STATUS = 'FAILED' WHERE PAYID = 42", log.read_text())

    def test_load_rejects_invalid_counts_and_bad_output(self):
        for value in ["0", "-1", "foo", "1.5"]:
            result = self.run_script("load_test.sh", "rpc", JOBS=value)
            self.assertNotEqual(result.returncode, 0)
        binary = self.executable("rpc binary", "print('not a balance')\n")
        result = self.run_script("load_test.sh", "rpc", MNP=binary, JOBS="1", REQUESTS="2")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("failed:   2", result.stdout)
        Path(binary).write_text("#!/usr/bin/env python3\nprint(123)\n")
        result = self.run_script("load_test.sh", "rpc", MNP=binary, JOBS="2", REQUESTS="3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("success:  3", result.stdout)

    def test_split_uses_valid_json_and_exact_amount(self):
        self.executable("curl", "import sys, json\n"
                        "args=sys.argv; request=json.loads(args[args.index('--data')+1])\n"
                        "assert request['method'] == 'split_integrated_address'\n"
                        "print(json.dumps({'result': {'payment_id': '000000000000002a'}}))\n")
        address = "5" * 106
        result = self.run_script("split_URI.sh", input="monero:" + address + "?tx_amount=1.2\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "000000000000002a " + address + " 1200000000000\n")

    def test_end_to_end_requires_receipt_not_just_submission(self):
        binary = self.executable("mnp", "print('monero:' + '5'*106 + '?tx_amount=0.000000055555')\n")
        db = self.executable("db", "import os, sys\n"
                             "query=sys.argv[-1]\n"
                             "if 'INSERT' in query: print(42)\n"
                             "elif 'SELECT STATUS' in query:\n"
                             "    assert 'WHERE PAYID = 42' in query\n"
                             "    print(os.environ['MOCK_STATUS'])\n")
        self.executable("curl", "import sys, json\n"
                        "args=sys.argv; request=json.loads(args[args.index('--data')+1])\n"
                        "if request['method'] == 'split_integrated_address':\n"
                        "    print(json.dumps({'result': {'payment_id': '000000000000002a'}}))\n"
                        "else: print(json.dumps({'result': {'tx_hash': 'a'*64}}))\n")
        for status, expected in [("COMPLETED", 0), ("FAILED", 1), ("", 1), ("REQUEST", 1)]:
            with self.subTest(status=status):
                result = self.run_script("end_to_end.sh", MNP=binary, DB_CMD=db,
                                         MOCK_STATUS=status, RECEIPT_TIMEOUT="1")
                self.assertEqual(result.returncode, expected, result.stderr)
                if expected == 0:
                    self.assertIn("Payment 42: COMPLETED", result.stdout)

    def test_detector_delayed_multiple_existing_and_timeout(self):
        watch = self.root / "transactions"
        watch.mkdir()
        tx = watch / ("a" * 64)
        tx.mkdir()
        log = self.root / "sql"
        db = self.executable("db", "import sys, os\n"
                             "query=sys.argv[-1]\n"
                             "with open(os.environ['SQL_LOG'], 'a') as f: f.write(query+'\\n')\n"
                             "if query.startswith('SELECT'): print('1000000000000')\n")
        # Include a FIFO present before startup, then add two to the same directory.
        existing = tx / "0000000000000001"
        os.mkfifo(existing)
        env = dict(self.env, WATCHDIR=str(watch), DB_CMD=db, SQL_LOG=str(log),
                   PIPE_TIMEOUT="2s", SCAN_INTERVAL="0.05")
        with tempfile.TemporaryFile() as output:
            detector = subprocess.Popen(["bash", str(TESTS / "txdetectdb.sh")], env=env,
                                        stdout=output, stderr=output, start_new_session=True)
            writers = []
            try:
                time.sleep(0.2)
                for identity, amount in [(1, "1e+12"), (2, "999"), (3, None)]:
                    fifo = tx / ("%016x" % identity)
                    if identity != 1:
                        os.mkfifo(fifo)
                    if amount is not None:
                        writers.append(subprocess.Popen(
                            ["python3", "-c", "import sys; f=open(sys.argv[1], 'w'); f.write(sys.argv[2]+'\\n'); f.close()",
                             str(fifo), amount]))
                deadline = time.monotonic() + 7
                while time.monotonic() < deadline:
                    sql = log.read_text() if log.exists() else ""
                    if all("STATUS = '%s' WHERE PAYID = %d" % pair in sql
                           for pair in [("COMPLETED", 1), ("FAILED", 2), ("TIMEOUT", 3)]):
                        break
                    time.sleep(0.05)
                else:
                    output.seek(0)
                    self.fail("Detector did not reach expected states: " + output.read().decode())
                self.assertEqual(sql.count("STATUS = 'WAITING' WHERE PAYID = 1"), 1)
            finally:
                detector.terminate()
                detector.wait(timeout=5)
                output.seek(0)
                self.assertNotIn("unbound variable", output.read().decode())
                for writer in writers:
                    if writer.poll() is None:
                        writer.terminate()
                    writer.wait(timeout=5)


class CliTests(unittest.TestCase):
    def call(self, *args):
        return subprocess.run([MNP, *args], input="", text=True, capture_output=True, timeout=5)

    def test_help_and_version(self):
        for args in [("help",), ("--help",), ("-h",), ("version",), ("--version",)] + [
                (command, "help") for command in
                ["init", "cleanup", "payment", "balance", "bc-height", "spend-proof", "tx-proof"]]:
            with self.subTest(args=args):
                result = self.call(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout)
                self.assertEqual(result.stderr, "")
        self.assertEqual(self.call("version").stdout, self.call("--version").stdout)

    def test_argument_errors_without_configuration_or_rpc(self):
        for args in [("--version", "foo"), ("--help", "foo"), ("balance", "foo"),
                     ("bc-height", "foo"), ("payment", "subaddr", "-1"),
                     ("payment", "list", "--amount", "1"), ("payment", "--amount", "bad"),
                     ("spend-proof",), ("tx-proof",), ("--notify-at", "4"),
                     ("--confirmation", "-1")]:
            with self.subTest(args=args):
                result = self.call(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
