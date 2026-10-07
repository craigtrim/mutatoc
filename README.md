# mutatoc

[![Build and tests](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml/badge.svg)](https://github.com/craigtrim/mutatoc/actions/workflows/test.yml)
[![Documentation](https://github.com/craigtrim/mutatoc/actions/workflows/docs.yml/badge.svg)](https://craigtrim.github.io/mutatoc/)
[![C17](https://img.shields.io/badge/C-17-00599C?logo=c&logoColor=white)](CMakeLists.txt)
[![Runtime dependencies: none](https://img.shields.io/badge/runtime%20dependencies-none-brightgreen)](THIRD_PARTY_NOTICES.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

**Turn your domain knowledge into fast, explainable entity extraction.**

Mutatoc recognizes multiword concepts even when text uses synonyms, changes word order, or separates the words. You define the vocabulary and relationships in an ontology; its exact, span and hierarchy matching stages turn that knowledge into canonical entities.

Use it to normalize course catalogs, tag technical documents, or enrich search with consistent concepts. Change what it recognizes by editing your ontology, without retraining a model.

With the included [course ontology](examples/courses.json):

| Input text | Matched concept | How it matches |
| --- | --- | --- |
| `applied mathematics` | `applied_mathematics` | Complete phrase |
| `applied maths` | `applied_mathematics` | Declared synonym |
| `mathematics, applied` | `applied_mathematics` | Proximity across punctuation and reversed word order |
| `advanced applied mathematics` | `advanced_applied_mathematics` | Longest phrase wins over the shorter concept inside it |

## Why use it?

- **Control what counts as a match.** Use span rules to recognize nearby words in different orders, and hierarchy matching to compose larger concepts from related terms.
- **Trace every result to the source.** Matches retain the original text, character offsets and nested match history, so your application can highlight the evidence behind each entity.
- **Keep extraction inside your application.** The C17 engine runs locally with no model, interpreter or runtime dependencies. Embed the C library, call the CLI, or keep an engine loaded through its JSON protocol.
- **Reuse your domain vocabulary.** Load Turtle, JSON or JSONL ontologies, combine multiple sources, and query their relationships through the same engine.

Recorded Windows benchmarks parse roughly 2,400-character documents in **3.4 to 6.5 ms** after loading. See [measurements and methodology](https://craigtrim.github.io/mutatoc/performance/).

## Try it

Build on Windows with CMake 3.20+ and Visual Studio 2019:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
.\build-msvc\Release\mutatoc.exe --ontology examples/courses.json --input-text "mathematics, applied"
```

Output: `applied_mathematics`. Add `--jsonf` to inspect the matched text, offsets and history. Windows and Linux are tested; see [build and integration instructions](https://craigtrim.github.io/mutatoc/).

Mutatoc is the MIT-licensed C port of [Mutato](https://github.com/craigtrim/mutato). The [documentation](https://craigtrim.github.io/mutatoc/) covers ontology authoring, the C API and JSON protocol, with [compatibility evidence and the full test inventory](https://craigtrim.github.io/mutatoc/compatibility/) available for evaluation.
