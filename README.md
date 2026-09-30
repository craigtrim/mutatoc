# mutatoc

[![C port compatibility](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml/badge.svg)](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml)
[![Version](https://img.shields.io/badge/version-0.3.0-blue)](include/mutatoc.h)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C17](https://img.shields.io/badge/C-17-00599C?logo=c&logoColor=white)](CMakeLists.txt)
[![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake&logoColor=white)](CMakeLists.txt)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)](.github/workflows/test.yml)
[![Upstream tests](https://img.shields.io/badge/Mutato%20tests-961%20replayed-brightgreen)](docs/compatibility.md)

<!-- craigtrim/mutatoc#1 -->

Mutatoc is the C17 port of [Mutato](https://github.com/craigtrim/mutato). It takes plain text and returns the ontology entities it contains. It accepts the same Turtle-encoded OWL ontologies and MDA JSON snapshots. Ontology extraction, cached and live finder behavior, collection merging, raw-text tokenization, and exact, span and hierarchy matching all run in C, in process, with no interpreter, model or worker.

Version 0.3.0 replaces the out-of-process tokenizer with a native one. Parsing a 2,400-character document is about ten times faster, the first parse no longer waits two seconds for a model to load, and the whole footprint drops from about 211 MB to 68 MB; every entity found before is still found. The release removes operations that served general NLP rather than ontology matching, so it breaks some callers. See [the changelog](CHANGELOG.md) and [performance](docs/performance.md#native-tokenization).

## Build

Use CMake 3.20 or newer and a C17 compiler. MSVC and GCC builds are tested on Windows; GCC with address and undefined-behavior sanitizers is tested on Linux.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Use the installed Visual Studio generator, or `-G Ninja -DCMAKE_BUILD_TYPE=Release` with an available compiler. The Windows MSVC build uses the static C runtime. `-DBUILD_SHARED_LIBS=ON` produces a DLL and import library. [examples/embed.c](examples/embed.c) compiles against the public header and demonstrates buffer ownership and prepared-token matching.

## Running

```powershell
.\build-msvc\Release\mutatoc.exe --ontology tests/fixtures/ontologies/animals-test.owl --input-text "Dog walks through London." --json
```

`scripts/package.cmake` assembles a relocatable Windows distribution from the static and shared builds, with a checksum manifest. See [packaging](docs/packaging.md).

## Axiom and C integration

`--serve` reads one UTF-8 JSON request per line and retains the loaded ontologies:

```json
{"op":"load","paths":["animals.owl","colors.owl"],"interface":"data","class_based":true}
{"op":"parse","text":"Dog walks through London."}
{"op":"query","interface":"data","method":"synonyms","args":[]}
```

Responses use `{"ok":true,"result":...}` or `{"ok":false,"error":...}`. Diagnostics go to stderr. Supplied token fields and nested swap histories are preserved.

The C API is `mc_create`, `mc_request`, `mc_free` and `mc_destroy`. Each engine owns its state; serialize access to an individual engine. Independent engines can run concurrently. See [the public header](include/mutatoc.h) and [the protocol](docs/protocol.md).

## Compatibility checks

The reference is Mutato revision `da6bfa5df80b208a3271e111f2921ad281d0da98`. `ctest` runs every check natively:

| Suite | What it covers |
| --- | --- |
| `upstream` | Mutato's 961 upstream tests, replayed request by request (3,930 recorded calls) |
| `parity` | 393 reference cases as prepared tokens and as raw text |
| `public_api` | 1,659 finder calls across 140 methods, plus match index lifecycle |
| `punctuation` | 16,165 authored punctuation and phrase-window contracts |
| `rdf` | The W3C RDF 1.1 Turtle suite (313 tests), 19 ontology graph fingerprints, typed literals |
| `extensions` | Optional stages, exact-window regressions, collections, external synonyms, prefixes |
| `tokenize` | The native tokenizer contract |
| `concurrency` | 24 engines on 8 threads through the public C API |
| `native`, `text`, `embedding` | Ontology loading, caller metadata, Unicode and URI handling, the embedding example |

[Compatibility notes](docs/compatibility.md) distinguish retained behavior, adaptations and repaired upstream defects. [Validation results](tests/validation.json) record the executed checks. [The implementation map](docs/implementation-map.json) accounts for Mutato's 91 source modules.
