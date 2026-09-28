"""Independent punctuation contracts using small, authored ontology fragments.

Expected matches come from the phrase used to author each ontology, not from
Mutato snapshots or a previous run of the engine under test.
"""
import argparse
from collections import Counter
import itertools
import json
import random
from pathlib import Path
import string
import subprocess
import sys
import tempfile
import time

PREFIXES = """@prefix : <https://example.org/punctuation#> .
@prefix owl: <http://www.w3.org/2002/07/owl#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix skos: <http://www.w3.org/2004/02/skos/core#> .
"""


class Client:
    def __init__(self, exe):
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen(
            [str(exe), "--serve", "--python", sys.executable],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
            text=True, encoding="utf-8",
        )

    def request(self, **request):
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            self.log.seek(0)
            raise AssertionError(self.log.read().decode("utf-8", errors="replace"))
        response = json.loads(line)
        return response

    def call(self, **request):
        response = self.request(**request)
        if not response["ok"]:
            raise AssertionError({"request": request, "response": response})
        return response["result"]

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        finally:
            self.process.stdout.close()
            self.log.close()
        if self.process.returncode:
            raise AssertionError(f"Engine exit: {self.process.returncode}")


def ontology(phrase, predicate="rdfs:seeAlso", suffix="", distractors=True):
    result = PREFIXES + ':Target a owl:Class; rdfs:label "Target course"; '
    result += predicate + " " + json.dumps(phrase, ensure_ascii=False) + suffix + " .\n"
    if distractors:
        result += ':History a owl:Class; rdfs:label "History" .\n'
        result += ':Other a owl:Class; rdfs:label "Other course"; rdfs:seeAlso "U.S. History since 1865" .\n'
    return result


def leaves(token):
    if "swaps" in token:
        return [leaf for child in token["swaps"]["tokens"] for leaf in leaves(child)]
    return [token]


def matches(result, canon="target"):
    return [t for t in result["tokens"] if t.get("swaps", {}).get("canon") == canon]


def visible(text):
    return "".join(text.split())


def prepared(parts):
    """Caller-supplied tokens with unrelated metadata to check lossless history."""
    result = []
    offset = 0
    for index, text in enumerate(parts):
        result.append(dict(id=f"source-{index}", x=offset, y=offset + len(text),
                           text=text, normal=text.strip().lower(),
                           provenance={"index": index, "original": text}))
        offset += len(text)
    return result


class Suite:
    def __init__(self, client, smoke=False):
        self.client = client
        self.smoke = smoke
        self.cases = Counter()
        self.assertions = 0
        self.failure_count = 0
        self.failures = []

    def equal(self, category, case, actual, expected):
        self.assertions += 1
        if actual != expected:
            self.failure_count += 1
            if len(self.failures) < 30:
                self.failures.append(dict(category=category, case=case,
                                          actual=actual, expected=expected))

    def load(self, text, **kwargs):
        return self.client.call(op="load", turtle=text, class_based=True,
                                interface="data", **kwargs)

    def tokenizer(self):
        exact = {
            "U.S. History to 1865": ["U", ".", "S", ". ", "History ", "to ", "1865"],
            "U.S.A.": ["U", ".", "S", ".", "A", "."],
            "Dog... Cat": ["Dog", ".", ".", ". ", "Cat"],
            "~~": ["~", "~"],
            "U~S~": ["U", "~", "S", "~"],
            "1.2.3": ["1.2.3"],
            "3.14": ["3.14"],
            "dr.": ["dr", "."],
            "mr.": ["mr", "."],
            "mrs.": ["mrs", "."],
            "": [],
        }
        for text, expected in exact.items():
            self.cases["tokenizer_explicit"] += 1
            self.equal("tokenizer_explicit", text, self.client.call(
                op="lingpatlab", method="tokenize_input_text", text=text), expected)
        alphabet = "US" if self.smoke else string.ascii_uppercase
        frames = [("", ""), ("(", ")"), ("[", "]"), ("~", "~~"),
                  ("\t", "\r\n"), ("😀 ", " café"), ("", "..."), ("\"", '\"')]
        for a, b in itertools.product(alphabet, repeat=2):
            for before, after in frames:
                text = before + a + "." + b + ". History to 1865" + after
                tokens = self.client.call(op="lingpatlab", method="tokenize_input_text", text=text)
                self.cases["tokenizer_initialisms"] += 1
                self.equal("tokenizer_initialisms", text, "".join(tokens), text)
                self.equal("tokenizer_initialisms", text + " tilde count", "".join(tokens).count("~"), text.count("~"))
        rng = random.Random(1865)
        symbols = "ABCXYZabcxyz019.~,;:!?()[]{} /\\\t\n\r_éΩ中😀"
        for index in range(20 if self.smoke else 3000):
            # Leading repeated periods prevent dictionary substitution; the
            # remaining alphabet deliberately excludes contraction/quote rules.
            text = ".." + "".join(rng.choice(symbols) for _ in range(rng.randint(1, 150)))
            tokens = self.client.call(op="lingpatlab", method="tokenize_input_text", text=text)
            self.cases["tokenizer_seeded_source_preservation"] += 1
            self.equal("tokenizer_seeded_source_preservation", index, "".join(tokens), text)

    def prepared_matching(self):
        alphabet = "US" if self.smoke else string.ascii_uppercase
        for a, b in itertools.product(alphabet, repeat=2):
            phrase = f"{a}.{b}. History to 1865"
            for predicate in ["rdfs:label", "rdfs:seeAlso", "skos:altLabel"]:
                self.load(ontology(phrase, predicate))
                parts = [a, ".", b, ". ", "History ", "to ", "1865"]
                original = prepared(parts)
                result = self.client.call(op="parse_tokens", tokens=original)
                found = matches(result)
                case = [phrase, predicate]
                self.cases["prepared_ontology_matching"] += 1
                self.equal("prepared_ontology_matching", case, len(found), 1)
                if found:
                    self.equal("prepared_ontology_matching", case + ["complete history"], leaves(found[0]), original)
                    self.equal("prepared_ontology_matching", case + ["exact"], found[0]["swaps"]["type"], "exact")
                # A different year must not become the target, even though the
                # generic History class can still match.
                negative = prepared(parts[:-1] + ["1866"])
                result = self.client.call(op="parse_tokens", tokens=negative)
                self.cases["prepared_negative_year"] += 1
                self.equal("prepared_negative_year", case, len(matches(result)), 0)

    def raw_matching(self):
        abbreviations = ["U.S.", "U.K.", "E.U.", "U.N.", "D.C.", "B.C.", "A.D.", "U.S.A.", "N.A.T.O."]
        years = ["1865", "1776", "2026"]
        predicates = ["rdfs:label", "rdfs:seeAlso", "skos:altLabel"]
        frames = [("", ""), ("(", ")"), ("[", "]"), ("\"", '\"'),
                  ("😀 ", " café"), ("\n\t", "\r\n"), ("~ ", " ~~"), ("prefix: ", "; suffix")]
        if self.smoke:
            abbreviations, years, predicates = abbreviations[:1], years[:1], predicates[:1]
        for abbr, year, predicate in itertools.product(abbreviations, years, predicates):
            phrase = f"{abbr} History to {year}"
            self.load(ontology(phrase, predicate))
            for before, after in frames:
                for spelling in [phrase, phrase.lower(), phrase.upper()]:
                    text = before + spelling + after
                    parsed = self.client.call(op="parse", text=text)
                    found = matches(parsed)
                    case = [predicate, text]
                    self.cases["raw_ontology_matching"] += 1
                    self.equal("raw_ontology_matching", case, len(found), 1)
                    if found:
                        original = "".join(t["text"] for t in leaves(found[0]))
                        self.equal("raw_ontology_matching", case + ["complete phrase"], visible(original), visible(spelling))
                    reconstructed = "".join(t["text"] for token in parsed["tokens"] for t in leaves(token))
                    self.equal("raw_ontology_matching", case + ["whole source"], visible(reconstructed), visible(text))
            for text in [phrase.replace(year, str(int(year) + 1)), phrase.replace("History", "Chemistry"), "X." + phrase, phrase.replace(".", "~"), phrase.replace(".", "")]:
                parsed = self.client.call(op="parse", text=text)
                self.cases["raw_negative"] += 1
                # A prefixed initial can leave a valid exact suffix; only the
                # suffix is allowed and may not absorb the extra initial.
                if text.startswith("X."):
                    for token in matches(parsed):
                        self.equal("raw_negative", text, visible("".join(t["text"] for t in leaves(token))), visible(phrase))
                else:
                    self.equal("raw_negative", text, len(matches(parsed)), 0)

    def contexts(self):
        phrase = "U.S. History to 1865"
        self.load(ontology(phrase))
        for separator in [" ", "  ", "\t", "\n", "\r\n", "\u00a0", "\u2003"]:
            text = separator.join(phrase.split(" "))
            parsed = self.client.call(op="parse", text=text)
            self.cases["internal_whitespace"] += 1
            self.equal("internal_whitespace", text, len(matches(parsed)), 1)
        for text in [phrase + " / " + phrase, phrase + "\n" + phrase,
                     phrase + " ... " + phrase, phrase + " ~~ " + phrase]:
            parsed = self.client.call(op="parse", text=text)
            self.cases["repeated_occurrences"] += 1
            self.equal("repeated_occurrences", text, len(matches(parsed)), 2)
        # Exercise ontology replacement and snapshot reload in one live engine.
        snapshot = self.client.call(op="snapshot")
        self.load(ontology("U.K. History to 1865"))
        self.equal("reload", "old phrase removed", len(matches(self.client.call(op="parse", text=phrase))), 0)
        self.client.call(op="load", snapshot=snapshot, name="roundtrip")
        self.equal("reload", "snapshot phrase restored", len(matches(self.client.call(op="parse", text=phrase))), 1)
        self.cases["reload"] += 2

    def window_boundaries(self):
        lengths = [1, 2, 6, 9, 10, 11, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128]
        if self.smoke:
            lengths = [10, 11]
        for size in lengths:
            words = [f"word{index}" for index in range(size)]
            phrase = " ".join(words)
            self.load(ontology(phrase))
            for gap in [" ", "  ", "\t", "\n", "\r\n", "\u00a0", "\u2003"]:
                source = [part for pair in zip(words, [gap] * size) for part in pair][:-1]
                for prefix, suffix in [([], []), (["before", " "], []), ([], [" ", "after"]), (["~", " "], [" ", "~"])]:
                    original = prepared(prefix + source + suffix)
                    result = self.client.call(op="parse_tokens", tokens=original)
                    found = matches(result)
                    case = [size, gap, prefix, suffix]
                    self.cases["window_boundaries"] += 1
                    self.equal("window_boundaries", case, len(found), 1)
                    if found:
                        self.equal("window_boundaries", case + ["history"], leaves(found[0]), original[len(prefix):len(original)-len(suffix) if suffix else None])
                # Punctuation is not whitespace and cannot be skipped.
                if size > 1:
                    for punctuation in ["~", ".", ",", ":", "!", "?", "(", ")"]:
                        interrupted = source.copy()
                        interrupted[1] = punctuation
                        result = self.client.call(op="parse_tokens", tokens=prepared(interrupted))
                        self.cases["punctuation_is_not_whitespace"] += 1
                        self.equal("punctuation_is_not_whitespace", [size, gap, punctuation], len(matches(result)), 0)

    def longest_leftmost(self):
        text = PREFIXES + '''
:Short a owl:Class; rdfs:label "U.S. History" .
:Long a owl:Class; rdfs:label "U.S. History to 1865" .
:Tail a owl:Class; rdfs:label "History to 1865" .
:Other a owl:Class; rdfs:label "U.S. History since 1865" .
'''
        for source in [text, PREFIXES + "\n".join(reversed(text[len(PREFIXES):].strip().splitlines()))]:
            self.load(source)
            for gap in [" ", "\t", "\n", "\r\n"]:
                phrase = gap.join(["U.S.", "History", "to", "1865"])
                for copies in [1, 2, 5, 10]:
                    input_text = " / ".join([phrase] * copies)
                    result = self.client.call(op="parse", text=input_text)
                    self.cases["longest_leftmost"] += 1
                    self.equal("longest_leftmost", [gap, copies], [t["swaps"]["canon"] for t in result["tokens"] if "swaps" in t], ["long"] * copies)

    def malformed_lookup(self):
        self.load(ontology("U.S. History to 1865"))
        snapshot = self.client.call(op="snapshot")
        tokens = prepared(["U", ".", "S", ".", "History", "to", "1865"])
        for lookup in [[], ["bad"], [dict(words=[])], 3, "bad", {"7": 3}, {"7": None}, {"7": "bad"}]:
            invalid = json.loads(json.dumps(snapshot))
            invalid["synonyms"]["lookup"] = lookup
            self.client.call(op="load", snapshot=invalid)
            result = self.client.request(op="parse_tokens", tokens=tokens)
            self.cases["malformed_lookup_recovery"] += 1
            self.equal("malformed_lookup_recovery", lookup, result.get("ok"), False)
            # The existing API rejects childless lookup values before matching
            # with its Empty ontology error. Nonempty malformed structures reach
            # the exact matcher's type validation.
            expected_code = 4 if not isinstance(lookup, (list, dict)) or not lookup else 2
            self.equal("malformed_lookup_recovery", lookup, result.get("error", {}).get("code"), expected_code)
            self.client.call(op="load", snapshot=snapshot)
            self.equal("malformed_lookup_recovery", [lookup, "recovered"], len(matches(self.client.call(op="parse_tokens", tokens=tokens))), 1)

    def run(self):
        for name in ["tokenizer", "prepared_matching", "raw_matching", "contexts", "window_boundaries", "longest_leftmost", "malformed_lookup"]:
            start = time.monotonic()
            getattr(self, name)()
            print(f"{name}: {sum(self.cases.values())} cases so far; {self.failure_count} failed assertions; {time.monotonic()-start:.2f}s", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()
    start = time.monotonic()
    client = Client(args.exe.resolve())
    suite = Suite(client, args.smoke)
    try:
        suite.run()
    finally:
        client.close()
    report = dict(cases=dict(suite.cases), total_cases=sum(suite.cases.values()),
                  assertions=suite.assertions, failed_assertions=suite.failure_count,
                  failures=suite.failures, seconds=round(time.monotonic()-start, 3))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return bool(suite.failure_count)


if __name__ == "__main__":
    sys.exit(main())
