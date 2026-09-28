"""Record window invalidation cases from the unmodified Python exact matcher."""
import copy
import hashlib
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".reference/src"))
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser.svc import PerformExactMatching

fwd = {
    "leaf": ["seed"],
    "pair": ["leaf b"],
    "triple": ["pair d"],
    "left": ["prefix leaf"],
    "right": ["b c"],
    "joined": ["triple right"],
    "shared_first": ["shared"],
    "shared_second": ["shared"],
    "ten": ["one two three four five six seven eight nine leaf"],
    "after": ["ten tail"],
    "wide": ["leaf one two three four five six seven eight nine"],
    "two_wide": ["leaf one two three four five six seven eight leaf"],
    "unicode": ["café"],
    "unicode_pair": ["unicode leaf"],
}
lookup, rev = {}, {}
for canon, words in fwd.items():
    for word in [canon, *words]:
        lookup.setdefault(str(len(word.split(" "))), []).append(word)
    for word in words:
        rev.setdefault(word, []).append(canon)
snapshot = {"synonyms": {"fwd": fwd, "rev": rev, "lookup": lookup}, "spans": {}, "entities": []}
matcher = PerformExactMatching(FindOntologyJSON(snapshot, "window-regression"))
texts = [
    "", "seed", "seed b c", "seed b c b c", "prefix seed b", "shared shared",
    "one two three four five six seven eight nine seed tail",
    "seed one two three four five six seven eight nine",
    "seed one two three four five six seven eight seed",
    "seed one two three four five six seven eight nine seed",
    "café seed b c", "seed z seed z seed", "seed " * 128,
    "z " * 160 + "seed b", "one two three four five six seven eight nine seed tail " * 20,
]
texts.append("seed b d")
rng = random.Random(20260926)
for _ in range(250):
    pieces = [rng.choice(["seed b d", "prefix seed", "seed", "b c", "shared", "café", "z",
                          "one two three four five six seven eight nine seed tail"])
              for _ in range(rng.randrange(1, 12))]
    texts.append(" ".join(pieces))
rows = []
for i, text in enumerate(texts):
    words = text.split()
    tokens = [{"id": j, "text": word + " ", "normal": word, "x": j * 12,
               "y": j * 12 + len(word), "ent": "", "pos": "NOUN", "head": 0,
               "custom_annotation": {"untouched": [j, word]}} for j, word in enumerate(words)]
    expected = matcher.process(copy.deepcopy(tokens))
    rows.append({"id": i, "text": text, "tokens": tokens, "expected": expected})
assert [t["normal"] for t in rows[2]["expected"]] == ["leaf", "right"]
assert rows[15]["expected"][0]["normal"] == "triple"
assert rows[6]["expected"][0]["normal"] == "after"
assert len(rows[12]["expected"]) == 128
out = {"reference_revision": "da6bfa5df80b208a3271e111f2921ad281d0da98",
       "generator": "scripts/generate_matching_regressions.py", "seed": 20260926,
       "source_sha256": hashlib.sha256((ROOT / ".reference/src/mutato/parser/svc/perform_exact_matching.py").read_bytes()).hexdigest(),
       "snapshot": snapshot, "cases": rows}
(ROOT / "tests/fixtures/api/matching-regressions.json").write_text(
    json.dumps(out, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"Recorded {len(rows)} complete token results from Python Mutato.")
