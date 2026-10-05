# mutatoc

[![C port compatibility](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml/badge.svg)](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml)
[![Documentation](https://github.com/craigtrim/mutatoc/actions/workflows/docs.yml/badge.svg)](https://craigtrim.github.io/mutatoc/)
[![Version](https://img.shields.io/badge/version-0.5.0-blue)](CHANGELOG.md)
[![TTL input](https://img.shields.io/badge/input-TTL-brightgreen)](docs/input-formats.md#ttl)
[![JSON input](https://img.shields.io/badge/input-JSON-brightgreen)](docs/input-formats.md#json)
[![Source parity checks](https://img.shields.io/badge/source%20parity-5%2C804%20checks-brightgreen)](docs/input-formats.md#implementation-and-verification)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C17](https://img.shields.io/badge/C-17-00599C?logo=c&logoColor=white)](CMakeLists.txt)
[![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake&logoColor=white)](CMakeLists.txt)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)](.github/workflows/test.yml)
[![Runtime dependencies: none](https://img.shields.io/badge/runtime%20dependencies-none-brightgreen)](THIRD_PARTY_NOTICES.md)
[![Sanitizers: ASan and UBSan](https://img.shields.io/badge/sanitizers-ASan%20%7C%20UBSan-brightgreen)](.github/workflows/test.yml)
[![Upstream tests](https://img.shields.io/badge/Mutato%20tests-961%20replayed-brightgreen)](docs/compatibility.md)

<!-- craigtrim/mutatoc#1, craigtrim/mutatoc#2 -->

Mutatoc takes plain text and returns the ontology entities it contains. Supply an ontology in **TTL or JSON**. Both are native inputs with the same graph queries and matching behavior, and a collection can contain both formats.

It is the C17 port of [Mutato](https://github.com/craigtrim/mutato). Ontology loading, tokenization, and matching run in process in C. Each reader adds facts directly to the shared runtime graph, without a whole-document conversion, temporary format, or converter process.

The [documentation site](https://craigtrim.github.io/mutatoc/) covers the input formats and C API. [Performance](docs/performance.md) records load and matching measurements, and the [changelog](CHANGELOG.md) records release changes.

## Build

Use CMake 3.20 or newer and a C17 compiler. MSVC and GCC builds are tested on Windows, and GCC is tested on Linux, including a build with address and undefined-behavior sanitizers. GitHub Actions runs the MSVC, Linux and sanitizer builds on every push.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Use the installed Visual Studio generator, or `-G Ninja -DCMAKE_BUILD_TYPE=Release` with an available compiler. The Windows MSVC build uses the static C runtime. `-DBUILD_SHARED_LIBS=ON` produces a DLL and import library. [examples/embed.c](examples/embed.c) compiles against the public header and demonstrates buffer ownership and prepared-token matching.

## TTL or JSON

These two files describe the same ontology. `canine` and `hound` both match `dog`, whose parent is `animal`.

### TTL

Save as `animals.ttl`, or use [examples/animals.ttl](examples/animals.ttl):

```turtle
@prefix : <http://example.org/animals#> .
@prefix owl: <http://www.w3.org/2002/07/owl#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix skos: <http://www.w3.org/2004/02/skos/core#> .

:Animal a owl:Class;
    rdfs:label "animal" .

:Dog a owl:Class;
    rdfs:label "dog";
    skos:altLabel "canine", "hound";
    rdfs:subClassOf :Animal;
    owl:backwardCompatibleWith "ANIMAL" .
```

### JSON

Save as `animals.json`, or use [examples/animals.json](examples/animals.json):

```json
[
  {"format":"mutatoc/1", "namespace":"http://example.org/animals#"},
  {"id":"Animal", "type":"owl:Class", "label":"animal"},
  {"id":"Dog", "type":"owl:Class", "label":"dog",
   "synonyms":["canine", "hound"], "parents":["Animal"], "ner":"ANIMAL"}
]
```

JSON uses the `mutatoc/1` ontology record schema. Fields such as `label`, `synonyms`, and `parents` name RDF predicates. Arbitrary predicates, typed literals, blank nodes, and ordered facts are available through the [full schema](docs/input-formats.md). JSONL accepts the same records one object per line.

### Run either file

```powershell
.\build-msvc\Release\mutatoc.exe --ontology examples/animals.ttl --input-text "a canine"
.\build-msvc\Release\mutatoc.exe --ontology examples/animals.json --input-text "a canine"
```

Both commands print `a dog`. Mutatoc detects the input format from the content. Use `--format ttl` or `--format json` to require one explicitly. Existing Turtle-encoded `.owl` files also work.

Add `--json` for the full result as compact JSON, or `--jsonf` for indented JSON. These flags select the output format for either ontology input. Each result includes the matched entity and its original tokens. `--stopwatch` writes elapsed time to stderr, and `--serve` keeps an engine open for JSON requests.

`--ontology FILE --snapshot OUT` writes a prepared MDA snapshot from either source format. A snapshot stores extracted matching views; a JSON ontology source retains the full RDF graph, including the facts used by direct graph queries. See [input formats](docs/input-formats.md) for the distinction and complete examples. JSON ontology sources are included in the current source build; the published 0.4.0 binaries predate this addition.

`scripts/package.cmake` assembles a relocatable Windows distribution from the static and shared builds, with a checksum manifest. See [packaging](docs/packaging.md).

## Input text

<!-- craigtrim/mutatoc#7 -->

Mutatoc tokenizes and matches text exactly as it is sent. The consumer knows why it sent the text in the form it did, so mutatoc makes no assumptions about how that text should have been written. A consumer that wants any of these does them before sending the text:

- Rejoining a spaced apostrophe, so `Driver ' s Ed` reads as `Driver's Ed`.
- Expanding contractions and abbreviations, such as `can't` to `can not` or `dept.` to `department`.
- Correcting spelling, OCR errors or transcription artifacts.

Every token's `text` is a slice of the input, the tokens concatenate back to it, and `x` and `y` are code point offsets into it. A matched entity's `text` is the input from its `x` to its `y`. Matching compares each token's `normal`, mutatoc's internal form of the text. It is lowercased, with every hyphen and dash folded to `-`, every apostrophe and single quote folded to `'`, and every double quote folded to `"`, so `Driver’s Ed` and `Driver's Ed` match the same synonym. The folding never touches `text`, `x` or `y`, so whatever hyphen, apostrophe or quote the input used is the one the output shows.

## Axiom and C integration

`--serve` reads one UTF-8 JSON request per line and retains the loaded ontologies:

```json
{"op":"load","paths":["animals.ttl","colors.json"],"interface":"data","class_based":true}
{"op":"parse","text":"Dog walks through London."}
{"op":"query","interface":"data","method":"synonyms","args":[]}
```

Responses use `{"ok":true,"result":...}` or `{"ok":false,"error":...}`. Diagnostics go to stderr. Supplied token fields and nested swap histories are preserved.

The C API is `mc_create`, `mc_request`, `mc_free` and `mc_destroy`. Each engine owns its state; serialize access to an individual engine. Independent engines can run concurrently. See [the public header](include/mutatoc.h) and [the protocol](docs/protocol.md).

## Compatibility checks

The reference is Mutato revision `da6bfa5df80b208a3271e111f2921ad281d0da98`. `ctest` runs every check natively:

| Suite | What it covers |
| --- | --- |
| `gold` | 11 hand-written paragraphs and documents across all seven fixture ontologies, whose 132 expected entities were written down before the engine ran |
| `upstream` | Mutato's 961 upstream tests, replayed request by request (3,930 recorded calls) |
| `parity` | 393 reference cases as prepared tokens and as raw text |
| `public_api` | 1,659 finder calls across 140 methods, plus match index lifecycle |
| `punctuation` | 16,165 authored punctuation and phrase-window contracts |
| `rdf` | The W3C RDF 1.1 Turtle suite (313 tests), 19 ontology graph fingerprints, typed literals |
| `sources` | 5,804 checks for JSON and JSONL, including 328 graph round-trips, 3,318 finder comparisons, 1,572 raw/prepared matching comparisons, optional stages, mixed collections, and failed loads |
| `extensions` | Optional stages, exact-window regressions, collections, external synonyms, prefixes |
| `tokenize` | The native tokenizer contract |
| `token_fidelity` | 3,244 cases that hold every token and entity to the input's exact text and offsets, across apostrophe, quote and dash variants in the input and in stored synonyms, whitespace, contractions, abbreviations, Unicode and invalid input |
| `span_distance` | 29,442 cases that hold every word of a span rule within its distance, across label widths, word orders, repeated words, punctuation and whitespace, pasted lists, stopwords, competing rules, direction flags and context words, checked against an oracle that tries every choice of occurrences |
| `concurrency` | 24 engines on 8 threads through the public C API |
| `native`, `text`, `embedding` | Ontology loading, caller metadata, Unicode and URI handling, the embedding example |
| `cli_json`, `cli_jsonf`, `cli_stopwatch` | The one-shot CLI's output formats |
| `performance` | Ceilings on load, first parse, parse and peak memory, run in optimized builds without sanitizers |

The gold corpus checks correctness rather than agreement: each text was audited against its ontology's vocabulary read straight from the OWL file, and its expectations were frozen before the first engine run. The one correction since then keeps the original expectation next to the OWL facts that justify the change (`tests/fixtures/gold/corpus.json`). Test fixtures keep their exact bytes on every platform (see `.gitattributes`), so Windows and Linux checkouts parse the same graphs. [Compatibility notes](docs/compatibility.md) distinguish retained behavior, adaptations and repaired upstream defects. [Validation results](tests/validation.json) record the executed checks. [The implementation map](docs/implementation-map.json) accounts for Mutato's 91 source modules. [Performance](docs/performance.md) covers the benchmark and earlier optimizations.
