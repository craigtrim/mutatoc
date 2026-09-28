# Speed comparison with Mutato

This compares mutatoc 0.2.2 with the Python reference, Mutato 1.1.1 (`da6bfa5`, cloned from https://github.com/craigtrim/mutato), on the same ontologies and inputs. Both implementations produced identical output in every comparison below.

## Setup

Measured on 2026-09-28 on an AMD Ryzen Threadripper 3960X running Windows 10. Both sides used Python 3.11.0, spaCy 3.8.2 and `en_core_web_sm` 3.8.0. mutatoc is the shipped `mutatoc-win-x64-0.2.2` package with its bundled runtime.

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

`OntologyParser(path)` against the `load` operation with a path.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 891 | 8.6 | 104x |
| econ-20160218 | 14,830 | 61.5 | 241x |
| medicopilot | 5,498 | 92.1 | 60x |
| courses-20251028 | 38,683 | 908 | 43x |

### Restore from a snapshot

`OntologyParser.from_dict` (including `json.loads`) against the `load` operation with an inline snapshot. Mutato's `MutatoAPI` constructor calls `spacy.load('en_core_web_sm')` every time, so about 370 ms of each Mutato figure is model loading. mutatoc starts its spaCy worker once, on the first raw-text parse.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 376 | 1.5 | 244x |
| econ-20160218 | 383 | 27.5 | 14x |
| medicopilot | 384 | 37.4 | 10x |
| courses-20251028 | 671 | 216 | 3.1x |

### Match pre-tokenized input

Every parity case for the ontology, run through `swap_input_tokens` against `parse_tokens`. This isolates the matching stages from spaCy. The time is the total for all cases in one round.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 89.3 | 22.5 | 4.0x |
| econ-20160218 | 163 | 163 | 1.0x |
| medicopilot | 17.1 | 21.8 | 0.8x |
| courses-20251028 | 87.4 | 249 | 0.4x |

mutatoc is slower on the two largest ontologies because its per-request matching cost grows with ontology size. A single unmatched token costs 0.11 ms on animals-test and 1.38 ms on courses-20251028. The exact stage rebuilds a hash set of every synonym up to the input length on each request (`src/match.c`, `exact`). The hierarchy stage checks candidates with a linear scan of the entity list (`contains`), and span rules are found with cJSON's linear key lookup. Mutato builds its dictionaries once, so its per-request cost does not depend on ontology size. Building these indexes at load time would remove the difference.

### Parse a document

A warm `swap_input_text` against the `parse` operation on a document of about 2,400 characters, built from each ontology's test phrases. This includes spaCy on both sides.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 314 | 183 | 1.7x |
| econ-20160218 | 236 | 142 | 1.7x |
| medicopilot | 335 | 221 | 1.5x |
| courses-20251028 | 1,854 | 211 | 8.8x |

### Cold one-shot run

Wall time for a new process that loads the model, builds the ontology from OWL and parses one phrase: `OntologyParser(path).parse(text)` in a fresh interpreter against `mutatoc --ontology path --input-text text`.

| Ontology | Mutato | mutatoc | Ratio |
| --- | ---: | ---: | ---: |
| animals-test | 2,990 | 1,873 | 1.6x |
| econ-20160218 | 17,207 | 2,038 | 8.4x |
| medicopilot | 7,709 | 2,002 | 3.9x |
| courses-20251028 | 40,934 | 2,781 | 14.7x |

About 1.8 seconds of each mutatoc cold run is starting the Python spaCy worker and loading the model.

## Reproducing

Clone Mutato at the reference revision and run the benchmark with a Python that has Mutato's pinned dependencies (`spacy==3.8.2`, `lingpatlab`, `rdflib`, `numpy==2.2.6` and the `en_core_web_sm` 3.8.0 archive from Mutato's `resources/lib`).

```powershell
git clone --branch v1.1.1 https://github.com/craigtrim/mutato.git .reference/bench/mutato
python scripts/benchmark_mutato.py --mutato .reference/bench/mutato --exe dist/mutatoc-win-x64-0.2.2/mutatoc.exe
```

The full report is written to `artifacts/benchmark-mutato.json`. `--only` limits the run to named ontologies, and `--runs` and `--build-runs` set the sample counts. The document generator drops trailing periods from test phrases, because mutatoc deliberately renders `..` as literal punctuation where Mutato emits tilde markers (see `tests/lingpatlab/punctuation-corrections.json`).
