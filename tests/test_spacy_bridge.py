"""Exercise the real C -> worker -> spaCy -> C path, including full JSON parity."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("MUTATOC_EXE", ROOT / "build" / ("mutatoc.exe" if os.name == "nt" else "mutatoc")))


class Client:
    def __init__(self, *options):
        self.log = tempfile.TemporaryFile()
        self.p = subprocess.Popen(
            [str(EXE), "--serve", "--python", sys.executable, *options],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
            text=True, encoding="utf-8",
        )

    def request(self, **q):
        self.p.stdin.write(json.dumps(q, ensure_ascii=False) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            self.log.seek(0)
            raise AssertionError(f"C process exited: {self.p.poll()}: {self.log.read().decode('utf-8', errors='replace')}")
        return json.loads(line)

    def call(self, **q):
        r = self.request(**q)
        if not r["ok"]:
            raise AssertionError(r)
        return r["result"]

    def close(self):
        if self.p.poll() is None:
            self.p.stdin.close()
            try:
                self.p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
                raise
        self.p.stdout.close()
        self.log.close()
        if self.p.returncode:
            raise AssertionError(f"C process exited with {self.p.returncode}")

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def alive(pid):
    if os.name == "nt":
        import ctypes
        k = ctypes.windll.kernel32
        k.OpenProcess.restype = ctypes.c_void_p
        handle = k.OpenProcess(0x00100000, False, pid)
        if not handle:
            return False
        try:
            k.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            return k.WaitForSingleObject(handle, 0) == 258
        finally:
            k.CloseHandle.argtypes = [ctypes.c_void_p]
            k.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


class SpacyBridgeTests(unittest.TestCase):
    def test_complete_reference_corpus(self):
        corpus = ROOT / "tests/fixtures/parity"
        checked = 0
        with Client() as c:
            info = c.call(op="spacy_info")
            self.assertEqual(info["spacy"], "3.8.2")
            self.assertEqual(info["preprocessing"], "mutatoc-c")
            self.assertEqual(info["lingpatlab_compatibility"], "1.1.1")
            self.assertEqual(info["model_version"], "3.8.0")
            for group in json.loads((corpus / "index.json").read_text(encoding="utf-8")):
                snapshot = json.loads((corpus / group["snapshot"]).read_text(encoding="utf-8"))
                c.call(op="load", name=group["name"], snapshot=snapshot)
                for case in json.loads((corpus / group["cases"]).read_text(encoding="utf-8")):
                    with self.subTest(ontology=group["name"], text=case["text"]):
                        tokens = c.call(op="tokenize", text=case["text"])
                        self.assertEqual(tokens, case["tokens"])
                        response = c.request(op="parse", text=case["text"], ctr=case["ctr"])
                        if "expected_error" in case:
                            self.assertFalse(response["ok"])
                        else:
                            self.assertTrue(response["ok"], response)
                            self.assertEqual(response["result"]["tokens"], case["expected_tokens"])
                            self.assertEqual(response["result"]["text"], case["expected_text"])
                    checked += 1
            self.assertEqual(info["pid"], c.call(op="spacy_info")["pid"])
        self.assertFalse(alive(info["pid"]), "Worker survived engine destruction")
        print(f"Complete raw-text/tokenization parity: {checked} cases; worker PID reused.", flush=True)

    def test_worker_crash_returns_error_then_restarts(self):
        with Client() as c:
            before = c.call(op="tokenize", text="Dog's collar in London.")
            pid = c.call(op="spacy_info")["pid"]
            os.kill(pid, signal.SIGTERM)
            time.sleep(0.1)
            failure = c.request(op="tokenize", text="Dog")
            self.assertFalse(failure["ok"])
            self.assertEqual(failure["error"]["code"], 5)
            after = c.call(op="tokenize", text="Dog's collar in London.")
            self.assertEqual(before, after)
            self.assertNotEqual(pid, c.call(op="spacy_info")["pid"])

    def test_missing_model_has_no_fallback(self):
        with Client("--spacy-model", "mutatoc_missing_model") as c:
            c.call(op="load", path=str(ROOT / "tests/fixtures/ontologies/animals-test.owl"))
            r = c.request(op="parse", text="Dog")
            self.assertFalse(r["ok"])
            self.assertIn("mutatoc_missing_model", r["error"]["message"])
            self.assertIn("initialization", r["error"]["message"])
            # The ontology and prepared-token API still work after a worker failure.
            self.assertEqual(c.call(op="parse_tokens", tokens=[])['tokens'], [])

    def test_transport_errors_unicode_paths_and_argument_quoting(self):
        with tempfile.TemporaryDirectory(prefix="mutatoc café & ") as tmp:
            script = Path(tmp) / "worker with spaces.py"
            pid_file = Path(tmp) / "pid.txt"
            script.write_text(
                f'import os,time\nfrom pathlib import Path\nPath({str(pid_file)!r}).write_text(str(os.getpid()))\ntime.sleep(30)\n',
                encoding="utf-8",
            )
            with Client("--spacy-worker", str(script), "--spacy-timeout", "300") as c:
                start = time.monotonic()
                r = c.request(op="tokenize", text="Dog")
                self.assertFalse(r["ok"])
                self.assertIn("timed out", r["error"]["message"])
                self.assertLess(time.monotonic() - start, 5)
                if pid_file.exists():
                    self.assertFalse(alive(int(pid_file.read_text())), "Timed-out worker survived cleanup")
            script.write_text('import sys\nprint("not JSON", flush=True)\n', encoding="utf-8")
            with Client("--spacy-worker", str(script)) as c:
                self.assertFalse(c.request(op="tokenize", text="Dog")["ok"])
            script.write_text(
                'import json,os,sys\n'
                'sys.stdin.reconfigure(encoding="utf-8");sys.stdout.reconfigure(encoding="utf-8")\n'
                'print(json.dumps({"ok":True,"result":{"protocol":1,"pid":os.getpid(),"model":sys.argv[-1]}}),flush=True)\n'
                'for line in sys.stdin:\n'
                ' q=json.loads(line)\n'
                ' print(json.dumps({"ok":True,"result":{"tokens":[{"text":q["text"],"orth":18446744073709551615}]}}),flush=True)\n',
                encoding="utf-8",
            )
            model = 'custom "model" ending\\'
            with Client("--spacy-worker", str(script), "--spacy-model", model) as c:
                info = c.call(op="spacy_info")
                self.assertEqual(info["model"], model)
                text = "café 😀\n\t" * 4000  # Exercise pipe buffering in both directions.
                self.assertTrue(c.call(op="lingpatlab", method="spacy_document", text=text) == {"tokens": [{"text": text, "orth": 18446744073709551615}]})
                c.call(op="configure_spacy", python=sys.executable, worker=str(script), model="replacement")
                self.assertFalse(alive(info["pid"]))
                self.assertEqual(c.call(op="spacy_info")["model"], "replacement")

    def test_write_timeout_cleans_worker_tree(self):
        with tempfile.TemporaryDirectory(prefix="mutatoc-blocked-worker-") as tmp:
            script = Path(tmp) / "blocked.py"
            script.write_text(
                'import json,os,time\n'
                'print(json.dumps({"ok":True,"result":{"protocol":1,"pid":os.getpid()}}),flush=True)\n'
                'time.sleep(30)\n', encoding="utf-8",
            )
            with Client("--spacy-worker", str(script), "--spacy-timeout", "1500") as c:
                pid = c.call(op="spacy_info")["pid"]
                start = time.monotonic()
                r = c.request(op="tokenize", text="x" * 200000)
                self.assertFalse(r["ok"])
                self.assertIn("timed out", r["error"]["message"])
                self.assertLess(time.monotonic() - start, 5)
                self.assertFalse(alive(pid), "Worker child survived blocked-write timeout")

    def test_one_shot_real_owl_preserves_statistical_annotations(self):
        result = subprocess.run(
            [str(EXE), "--python", sys.executable, "--ontology",
             str(ROOT / "tests/fixtures/ontologies/animals-test.owl"),
             "--input-text", "Dog walks through London.", "--json"],
            capture_output=True, text=True, encoding="utf-8", timeout=120, check=True,
        )
        out = json.loads(result.stdout)
        london = next(t for t in out["tokens"] if t["normal"] == "london")
        self.assertEqual(london["ent"], "GPE")
        self.assertTrue(london["pos"])
        self.assertTrue(london["dep"])
        original = out["tokens"][0]["swaps"]["tokens"][0]
        self.assertTrue(original["tag"])
        self.assertEqual(original["other"]["orth"], 1390790794094431770)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path)
    args, rest = parser.parse_known_args()
    if args.exe:
        EXE = args.exe.resolve()
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)
