# mutatoc

Mutatoc is the C17 port of [Mutato](https://github.com/craigtrim/mutato). It accepts the same Turtle-encoded OWL ontologies and MDA JSON snapshots. Ontology extraction, cached/live finder behavior, collection merging, and exact/span/hierarchy matching run in C.

LingPatLab 1.1.1 is implemented in C, including tokenization, token construction, normalization, stemming, WordNet membership, segmentation, entity extraction and text utilities. The runtime no longer installs LingPatLab, wordnet-lookup or unicodedata2. A small Python worker exposes spaCy 3.8.2 and `en_core_web_sm` 3.8.0 model operations; the C engine applies the linguistic rules. A separate RDFLib 7.1.4 worker handles arbitrary SPARQL and normalization of date, duration, base64 and XML literals. These workers start lazily and persist per engine. Prepared tokens, snapshots and linguistic operations that do not use the trained model run without Python. [Native LingPatLab APIs](docs/lingpatlab.md) describe the complete port and source-specific behavior.

Version 0.2.2 fixes dotted-abbreviation matching, preserves literal punctuation, and removes the ten-token exact-match cutoff. Whitespace inside an exact phrase remains in its source history. See [punctuation regression coverage](docs/punctuation.md). The 0.2.1 performance improvements remain in place; see [performance and validation](docs/performance.md). [Speed comparison with Mutato](docs/mutato-comparison.md) measures both implementations on the same ontologies and inputs.

## Windows build

Use CMake 3.20 or newer and a C17 compiler. MSVC and GCC builds have been tested on Windows; GCC with address/undefined-behavior sanitizers has been tested on Linux.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 16 2019" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Use the installed Visual Studio generator, or `-G Ninja -DCMAKE_BUILD_TYPE=Release` with an available compiler. The Windows MSVC build uses the static C runtime. `-DBUILD_SHARED_LIBS=ON` produces a DLL and import library. [examples/embed.c](examples/embed.c) compiles against the public header and demonstrates buffer ownership and prepared-token matching.

## Complete runtime

The original repository contains the pinned model archive. Python 3.11 installs the verified dependency set:

```powershell
py -3.11 scripts/setup_spacy.py --with-sparql --model-archive ..\mutato\resources\lib\en_core_web_sm-3.8.0.tar.gz
$env:MUTATOC_PYTHON = "$PWD\.runtime\Scripts\python.exe"
.\build-msvc\Release\mutatoc.exe --ontology tests/fixtures/ontologies/animals-test.owl --input-text "Dog walks through London." --json
```

The setup script verifies the model SHA-256. Omit `--with-sparql` only when arbitrary SPARQL and the special literal types are unnecessary. Missing dependencies produce errors; the engine never substitutes approximate tokenization.

For a relocatable Windows distribution, build both library variants and run:

```powershell
cmake -S . -B build-shared -G "Visual Studio 16 2019" -A x64 -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF
cmake --build build-shared --config Release
py -3.11 scripts/package_windows.py --zip
```

The package contains the executable, static library, DLL/import library, header, workers, Python runtime, model, dependency distributions with their licenses, and a file checksum manifest. Its CLI discovers `runtime/python/python.exe` beside the executable. Python does not need to be on `PATH`. The packaging script checks that configuration before creating the archive. See [packaging](docs/packaging.md).

## Axiom and C integration

`--serve` reads one UTF-8 JSON request per line and retains loaded ontologies and workers:

```json
{"op":"load","paths":["animals.owl","colors.owl"],"interface":"data","class_based":true}
{"op":"parse","text":"Dog walks through London."}
{"op":"query","interface":"data","method":"synonyms","args":[]}
{"op":"sparql","query":"SELECT ?s ?label WHERE {?s rdfs:label ?label} ORDER BY ?s ?label","result_type":21}
```

Responses use `{"ok":true,"result":...}` or `{"ok":false,"error":...}`. Diagnostics go to stderr. Full tokens, numeric identifiers and nested swap histories are preserved.

The C API uses `mc_create`, `mc_request`, `mc_free` and `mc_destroy`. Configure the retained workers with `mc_use_spacy` and `mc_use_sparql` before raw-text parsing or operations requiring RDFLib compatibility. Each engine owns its state; serialize access to an individual engine. Independent engines can run concurrently. See [the public header](include/mutatoc.h) and [the protocol](docs/protocol.md).

## Compatibility checks

The reference is Mutato revision `da6bfa5df80b208a3271e111f2921ad281d0da98`. The repository retains all 961 Mutato tests and all 19 OWL fixtures, plus complete token goldens, public API queries, optional-stage cases, and the W3C Turtle corpus. LingPatLab has its own 17 unchanged upstream tests and 8,287 differential cases across 60 methods.

```powershell
cmake -S . -B build-msvc -DMUTATOC_TEST_PYTHON="$PWD/.runtime/Scripts/python.exe"
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
.\.runtime\Scripts\python.exe -m pip install -r tests/upstream/requirements.txt
$env:MUTATOC_EXE = "$PWD/build-msvc/Release/mutatoc.exe"
.\.runtime\Scripts\python.exe scripts/run_upstream.py
.\.runtime\Scripts\python.exe scripts/run_lingpatlab_upstream.py --exe $env:MUTATOC_EXE
```

Without `MUTATOC_TEST_PYTHON`, CTest runs the native embedding, text, ontology, prepared-token and query-corpus tests. With it, CTest also runs the real workers, native LingPatLab comparisons and W3C graph comparisons. The integration suite verifies that the removed LingPatLab packages are absent. The complete runtime must be installed for those integration checks.

[Compatibility notes](docs/compatibility.md) distinguish retained behavior, language adaptations and repaired upstream defects. [Validation results](tests/validation.json) record the executed checks. [The implementation map](docs/implementation-map.json) accounts for the 91 source modules. GitHub work items are recorded in [docs/issues/index.json](docs/issues/index.json).
