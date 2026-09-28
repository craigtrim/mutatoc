# Speed comparison with Mutato

This compares mutatoc with the Python reference, Mutato 1.1.1 (`da6bfa5`, cloned from https://github.com/craigtrim/mutato), on the same ontologies and inputs. Both implementations produced identical output in every comparison below.

The mutatoc figures are for 0.2.3, which adds the match index described in [performance](performance.md#match-index). The 0.2.2 figures appear alongside where the index changed them.

## Setup

Measured on 2026-09-28 on an AMD Ryzen Threadripper 3960X running Windows 10. Both sides used Python 3.11.0, spaCy 3.8.2 and `en_core_web_sm` 3.8.0. mutatoc ran with the bundled runtime from its Windows package and the 0.2.3 executable.

mutatoc runs through `--serve`, so its timings include JSON encoding and the pipe round trip (about 0.02 ms per request). Mutato runs in process. Construction from OWL runs in a fresh process for every sample on both sides, and the reported time covers only the construction call. Figures are medians of 10 runs, except construction and cold runs, which are medians of 3.

| Ontology | OWL size | Triples | Entities | Test cases |
| --- | ---: | ---: | ---: | ---: |
| animals-test | 11 KB | 208 | 60 | 76 |
| econ-20160218 | 181 KB | 2,520 | 461 | 93 |
| medicopilot | 335 KB | 4,512 | 684 | 11 |
| courses-20251028 | 1.7 MB | 28,241 | 5,860 | 34 |

## Results

Times are in milliseconds. The ratio is Mutato's time divided by mutatoc's, so a value above 1 means mutatoc is faster.

### Build from OWL

`OntologyParser(path)` against the `load` operation with a path. The mutatoc time includes building the match index.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 882 | 8.9 | 99x |
| econ-20160218 | 14,616 | 61.8 | 236x |
| medicopilot | 5,542 | 93.0 | 60x |
| courses-20251028 | 38,354 | 641 | 60x |

### Restore from a snapshot

`OntologyParser.from_dict` (including `json.loads`) against the `load` operation with an inline snapshot. Mutato's `MutatoAPI` constructor calls `spacy.load('en_core_web_sm')` every time, so about 370 ms of each Mutato figure is model loading. mutatoc starts its spaCy worker once, on the first raw-text parse, and its figure includes building the match index.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 365 | 1.5 | 239x |
| econ-20160218 | 370 | 31.6 | 12x |
| medicopilot | 390 | 43.2 | 9.0x |
| courses-20251028 | 524 | 244 | 2.2x |

### Match pre-tokenized input

Every parity case for the ontology, run through `swap_input_tokens` against `parse_tokens`. This isolates the matching stages from spaCy. The time is the total for all cases in one round.

| Ontology | Mutato | mutatoc 0.2.2 | mutatoc | Ratio |
| --- | ---: | ---: | ---: | ---: |
| animals-test | 88.3 | 22.5 | 18.2 | 4.9x |
| econ-20160218 | 148 | 163 | 48.5 | 3.1x |
| medicopilot | 15.6 | 21.8 | 4.0 | 3.9x |
| courses-20251028 | 76.2 | 249 | 12.3 | 6.2x |

In 0.2.2, the per-request cost grew with ontology size, because the exact stage rebuilt a hash set of every synonym on each request and other stages scanned JSON objects linearly. A single unmatched token cost 1.38 ms on courses-20251028. The match index moves that work to load time, and the same request now costs about 0.1 ms on every ontology.

### Parse a document

A warm `swap_input_text` against the `parse` operation on a document of about 2,400 characters, built from each ontology's test phrases. This includes spaCy on both sides, which accounts for most of the mutatoc time.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 306 | 176 | 1.7x |
| econ-20160218 | 228 | 130 | 1.8x |
| medicopilot | 334 | 211 | 1.6x |
| courses-20251028 | 1,881 | 194 | 9.7x |

### Cold one-shot run

Wall time for a new process that loads the model, builds the ontology from OWL and parses one phrase: `OntologyParser(path).parse(text)` in a fresh interpreter against `mutatoc --ontology path --input-text text`.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 2,889 | 1,793 | 1.6x |
| econ-20160218 | 16,666 | 1,906 | 8.7x |
| medicopilot | 7,623 | 1,978 | 3.9x |
| courses-20251028 | 40,182 | 2,471 | 16.3x |

About 1.8 seconds of each mutatoc cold run is starting the Python spaCy worker and loading the model.

## Reproducing

Clone Mutato at the reference revision and run the benchmark with a Python that has Mutato's pinned dependencies (`spacy==3.8.2`, `lingpatlab`, `rdflib`, `numpy==2.2.6` and the `en_core_web_sm` 3.8.0 archive from Mutato's `resources/lib`).

```powershell
git clone --branch v1.1.1 https://github.com/craigtrim/mutato.git .reference/bench/mutato
python scripts/benchmark_mutato.py --mutato .reference/bench/mutato --exe dist/mutatoc-win-x64-0.2.3/mutatoc.exe
```

The full report is written to `artifacts/benchmark-mutato.json`. `--only` limits the run to named ontologies, and `--runs` and `--build-runs` set the sample counts. The document generator drops trailing periods from test phrases, because mutatoc deliberately renders `..` as literal punctuation where Mutato emits tilde markers (see `tests/lingpatlab/punctuation-corrections.json`).
