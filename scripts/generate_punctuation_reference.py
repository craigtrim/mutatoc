"""Generate identified corrections with the Python reference, never Mutatoc.

The original differential corpus is left untouched. This overlays only the
outcomes changed by removing tokenizer sentinels. Python's original punctuation,
quote, whitespace, spaCy, and token construction implementations remain the
independent oracle. No C executable is invoked or read by this script.
"""
import argparse
import hashlib
import json
import logging
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--output", type=Path)
args = parser.parse_args()
root = args.root.resolve()
sys.path.insert(0, str(root / ".reference/lingpatlab"))
from lingpatlab import LingPatLab
from lingpatlab.tokenizer.svc import TokenizeUseGraffl
from lingpatlab.tokenizer.dmo import DictionaryFinder
import spacy


def corrected_process(self, text):
    tokens = self._replace_enclitics(self._split(text))
    dictionary = DictionaryFinder.abbreviations()
    tokens = [dictionary[token].replace("~~", ".")
              if token.count(".") < 2 and token in dictionary else token
              for token in tokens]
    return self._handle_lingering_squotes(self._redelimit_spaces(self._handle_punkt(tokens)))


def clean(value):
    if hasattr(value, "to_json"):
        return clean(value.to_json())
    if isinstance(value, dict):
        return {str(k): clean(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [clean(v) for v in value]
    return value


logging.disable(logging.CRITICAL)
TokenizeUseGraffl.process = corrected_process
nlp = spacy.load("en_core_web_sm")
api = LingPatLab()
tokenize = TokenizeUseGraffl().process
baseline = root / "tests/lingpatlab/parity.json"
rows = json.loads(baseline.read_text(encoding="utf-8"))
corrections = []
for index, row in enumerate(rows):
    request = row["request"]
    if request["method"] not in ["tokenize_input_text", "parse_input_text"]:
        continue
    try:
        result = tokenize(request["text"]) if request["method"] == "tokenize_input_text" else api.parse_input_text(request["text"], nlp)
        expected = {"expected": clean(result)}
    except Exception as exc:
        expected = {"error": type(exc).__name__}
    previous = {key: row[key] for key in ["expected", "error"] if key in row}
    if expected != previous:
        corrections.append({"index": index, "request": request, **expected})
source = root / ".reference/lingpatlab/lingpatlab/tokenizer/svc/tokenize_use_graffl.py"
report = {
    "reason": "Preserve literal punctuation instead of emitting temporary tilde markers.",
    "reference_revision": "2ed920f1bc7e57d8c74b5b35a54e90f2b7f8ed71",
    "baseline_sha256": hashlib.sha256(baseline.read_bytes().replace(b"\r\n", b"\n")).hexdigest(),
    "reference_tokenizer_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "corrections": corrections,
}
output = args.output or root / "tests/lingpatlab/punctuation-corrections.json"
output.write_text(json.dumps(report, ensure_ascii=True, indent=1) + "\n", encoding="utf-8")
print(f"Independent Python reference corrections: {len(corrections)}; original {len(rows)}-case corpus preserved.")
