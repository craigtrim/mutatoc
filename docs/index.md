# mutatoc

Mutatoc is the C17 port of [Mutato](https://github.com/craigtrim/mutato). It takes plain text and returns the ontology entities it contains. It accepts the same Turtle-encoded OWL ontologies and MDA JSON snapshots as Mutato, and ontology extraction, the finder queries, tokenization and matching all run in process in C. Nothing else is installed or started at run time.

## Build

Use CMake 3.20 or newer and a C17 compiler. MSVC and GCC are tested on Windows, and GCC on Linux.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

`-DBUILD_SHARED_LIBS=ON` produces a DLL and import library.

## Run

```powershell
.\build-msvc\Release\mutatoc.exe --ontology tests/fixtures/ontologies/animals-test.owl --input-text "Dog walks through London."
```

This prints `dog walks through London .`, where `dog` is the matched entity. `--json` prints the full result as compact JSON and `--jsonf` prints it indented. `--serve` keeps an engine open and reads one JSON request per line.

## Embed

The C API is `mc_create`, `mc_request`, `mc_free` and `mc_destroy`. Each engine owns its state, so serialize access to one engine; independent engines can run concurrently. Requests and responses use the same JSON as `--serve`. See [JSON and C integration](protocol.md) and [the public header](https://github.com/craigtrim/mutatoc/blob/master/include/mutatoc.h).

## Pages

- [JSON and C integration](protocol.md) lists every operation with its inputs and results, and covers errors and buffer ownership.
- [Compatibility](compatibility.md) states what matches Mutato, what was adapted for C, and which upstream defects were repaired.
- [Performance](performance.md) has the benchmark for native tokenization and the earlier matching optimizations.
- [Punctuation](punctuation.md) covers punctuation and exact phrase matching.
- [Windows distribution](packaging.md) builds the relocatable package.
- [Coding style](coding-style.md) is for contributors.
