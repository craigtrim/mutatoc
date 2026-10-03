# mutatoc

[![TTL input](https://img.shields.io/badge/input-TTL-brightgreen)](input-formats.md#ttl)
[![JSON input](https://img.shields.io/badge/input-JSON-brightgreen)](input-formats.md#json)
[![Source parity checks](https://img.shields.io/badge/source%20parity-5%2C804%20checks-brightgreen)](input-formats.md#implementation-and-verification)

Mutatoc takes plain text and returns the ontology entities it contains. Supply an ontology in **TTL or JSON**. Both are native inputs with the same graph queries and matching behavior, and a collection can contain both formats.

It is the C17 port of [Mutato](https://github.com/craigtrim/mutato). Each reader adds facts directly to the shared runtime graph. Ontology extraction, tokenization, and matching run in process in C, without a whole-document conversion or converter process.

| Ontology input | How to write it |
| --- | --- |
| [TTL](input-formats.md#ttl) | Turtle statements with prefixes and RDF predicates; Turtle-encoded `.owl` files also work |
| [JSON](input-formats.md#json) | An array of ontology records, with fields such as `id`, `label`, `synonyms`, and `parents`, plus arbitrary RDF facts |

[Ontology input formats](input-formats.md) shows the same ontology in both forms and defines the JSON record schema. JSONL uses those records one object per line. Prepared MDA snapshots remain available for storing and restoring extracted matching views.

## Build

Use CMake 3.20 or newer and a C17 compiler. MSVC and GCC are tested on Windows, and GCC on Linux.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

`-DBUILD_SHARED_LIBS=ON` produces a DLL and import library.

JSON ontology sources are included in the current source build. The published 0.4.0 binaries predate this addition; see the [changelog](https://github.com/craigtrim/mutatoc/blob/master/CHANGELOG.md).

## Run

```powershell
.\build-msvc\Release\mutatoc.exe --ontology examples/animals.ttl --input-text "a canine"
.\build-msvc\Release\mutatoc.exe --ontology examples/animals.json --input-text "a canine"
```

Both commands print `a dog`. The files are included in the repository's `examples` directory and contain the same facts. Mutatoc detects the source format from the content; `--format ttl` and `--format json` select it explicitly.

`--json` prints the full result as compact JSON and `--jsonf` prints it indented, with either source format. These are output flags. `--serve` keeps an engine open and reads one JSON request per line. See [loading an ontology](protocol.md#load-ttl-or-json) for the corresponding API requests.

## Input text

<!-- craigtrim/mutatoc#7 -->

Mutatoc tokenizes and matches text exactly as it is sent. The consumer knows why it sent the text in the form it did, so mutatoc makes no assumptions about how that text should have been written. A consumer that wants any of these does them before sending the text:

- Rejoining a spaced apostrophe, so `Driver ' s Ed` reads as `Driver's Ed`.
- Expanding contractions and abbreviations, such as `can't` to `can not` or `dept.` to `department`.
- Correcting spelling, OCR errors or transcription artifacts.

Every token's `text` is a slice of the input, the tokens concatenate back to it, and `x` and `y` are code point offsets into it. A matched entity's `text` is the input from its `x` to its `y`. [Raw-text tokens](protocol.md#raw-text-tokens) gives the full rules. Matching compares each token's `normal`, mutatoc's internal form of the text. It is lowercased, with every hyphen and dash folded to `-`, every apostrophe and single quote folded to `'`, and every double quote folded to `"`, so `Driver’s Ed` and `Driver's Ed` match the same synonym. The folding never touches `text`, `x` or `y`, so whatever hyphen, apostrophe or quote the input used is the one the output shows.

## Embed

The C API is `mc_create`, `mc_request`, `mc_free` and `mc_destroy`. Each engine owns its state, so serialize access to one engine; independent engines can run concurrently. Requests and responses use the same JSON as `--serve`. See [JSON and C integration](protocol.md) and [the public header](https://github.com/craigtrim/mutatoc/blob/master/include/mutatoc.h).

## Pages

- [Ontology input formats](input-formats.md) describes TTL and JSON, JSONL framing, complete RDF terms, and mixed-format collections.
- [JSON and C integration](protocol.md) lists every operation with its inputs and results, and covers errors and buffer ownership.
- [Compatibility](compatibility.md) states what matches Mutato, what was adapted for C, and which upstream defects were repaired.
- [Performance](performance.md) has the benchmark for native tokenization and the earlier matching optimizations.
- [Punctuation](punctuation.md) covers punctuation and exact phrase matching.
- [Windows distribution](packaging.md) builds the relocatable package.
- [Coding style](coding-style.md) is for contributors.
