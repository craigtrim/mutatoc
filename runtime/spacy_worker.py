"""Persistent spaCy model interface. Linguistic rules run in the C engine."""
import argparse
import contextlib
import json
import os
import sys

PROTOCOL = 1


def send(value):
    sys.stdout.write(json.dumps(value, ensure_ascii=False, allow_nan=False) + "\n")
    sys.stdout.flush()


def document_json(doc):
    # Expose model attributes without applying LingPatLab rules or normalization.
    tokens = []
    for token in doc:
        tokens.append({
            "text": token.text, "lemma": token.lemma_, "morph": token.morph.to_dict(),
            "sentiment": token.sentiment, "pos": token.pos_, "tag": token.tag_,
            "dep": token.dep_, "ent": token.ent_type_, "shape": token.shape_,
            "is_alpha": token.is_alpha, "is_stop": token.is_stop,
            "other": {"i": token.i, "idx": token.idx, "orth": token.orth,
                      "head_i": token.head.i, "head_idx": token.head.idx,
                      "head_orth": token.head.orth, "head_text": token.head.text},
        })
    return {"tokens": tokens, "sentences": [str(sent) for sent in doc.sents]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default="en_core_web_sm")
    args = parser.parse_args()
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import spacy
            nlp = spacy.load(args.model)
        send({"ok": True, "result": {
            "protocol": PROTOCOL, "pid": os.getpid(), "python": sys.version.split()[0],
            "spacy": spacy.__version__, "preprocessing": "mutatoc-c",
            "lingpatlab_compatibility": "1.1.1", "model": args.model,
            "model_name": nlp.meta.get("name"), "model_version": nlp.meta.get("version"),
            "pipeline": nlp.pipe_names, "operations": ["analyze", "retokenize"],
        }})
    except Exception as exc:
        send({"ok": False, "error": {"message": f"spaCy worker initialization: {type(exc).__name__}: {exc}"}})
        return 1
    doc = None
    for line in sys.stdin:
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("Worker requires an object request")
            with contextlib.redirect_stdout(sys.stderr):
                if request.get("op") == "analyze":
                    text = request.get("text")
                    if not isinstance(text, str):
                        raise ValueError("text must be a string")
                    doc = nlp(text)
                elif request.get("op") == "retokenize":
                    if doc is None:
                        raise ValueError("Analyze a document before retokenization")
                    spans = request.get("spans")
                    if not isinstance(spans, list):
                        raise ValueError("spans must be an array")
                    with doc.retokenize() as retokenizer:
                        for span in spans:
                            if not isinstance(span, list) or len(span) != 2 or not all(type(i) is int for i in span):
                                raise ValueError("Each span requires integer start/end indices")
                            start, end = span
                            if not 0 <= start < end <= len(doc):
                                raise ValueError("Invalid retokenization span")
                            try:
                                retokenizer.merge(doc[start:end])
                            except ValueError as exc:
                                # spaCy rejects overlapping spans. Keep the valid merges.
                                print(exc, file=sys.stderr)
                else:
                    raise ValueError("Worker requires analyze or retokenize")
                result = document_json(doc)
            send({"ok": True, "result": result})
        except Exception as exc:
            send({"ok": False, "error": {"message": f"{type(exc).__name__}: {exc}"}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
