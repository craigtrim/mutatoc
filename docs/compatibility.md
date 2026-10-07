# Compatibility contract

Reference: Mutato `da6bfa5df80b208a3271e111f2921ad281d0da98`.

The logical port preserves ontology extraction and matching, including the differences between direct OWL queries, the live finder, the JSON finder and the low-level JSON API. The same OWL files and prepared snapshots remain inputs, alongside native [JSON and JSONL ontology sources](input-formats.md). Mutato's class instances and callbacks are represented by an opaque C engine and JSON operations.

## Runtime

Everything runs in process, with no interpreter, model or worker. Since 0.3.0 raw text is tokenized natively (see [the protocol](protocol.md#raw-text-tokens)); tokens carry `id`, `text`, `x`, `y` and `normal`, the fields matching reads. Native TTL and JSON readers build the same graph and indexes, including relative IRI resolution, and every finder query runs natively. Arbitrary SPARQL is not provided; `triples` exports the graph from either input format. JSONL supplies the same ontology records as JSON, one per line.

## Test suites

`ctest` runs every check natively. For an MSVC Release build:

```powershell
ctest --test-dir build-msvc -C Release --output-on-failure
```

| Suite | What it covers |
| --- | --- |
| `gold` | 11 hand-written paragraphs and documents across all seven fixture ontologies, whose 132 expected entities were written down before the engine ran |
| `upstream` | Mutato's 961 upstream tests, replayed request by request (3,930 recorded calls) |
| `parity` | 393 reference cases as prepared tokens and as raw text |
| `public_api` | 1,659 finder calls across 140 methods, plus match index lifecycle |
| `punctuation` | 16,165 authored punctuation and phrase-window contracts |
| `comma_synonyms` | 19,968 whole-literal cases and 5,760 fragment-rejection cases across Turtle, JSON, JSONL and snapshots, plus comma-list options, numeric commas, live matching, collections and reloads |
| `rdf` | The W3C RDF 1.1 Turtle suite (313 tests), 19 ontology graph fingerprints, typed literals |
| `sources` | 5,804 checks for JSON and JSONL, including 328 graph round-trips, 3,318 finder comparisons, 1,572 raw/prepared matching comparisons, optional stages, mixed collections, and failed loads |
| `extensions` | Optional stages, exact-window regressions, collections, external synonyms, prefixes |
| `tokenize` | The native tokenizer contract |
| `token_fidelity` | 3,244 cases that hold every token and entity to the input's exact text and offsets, across apostrophe, quote and dash variants in the input and in stored synonyms, whitespace, contractions, abbreviations, Unicode and invalid input |
| `span_distance` | 39,111 cases that hold every word of a span rule within its distance, turn every group of span words into a span and give whitespace no position, across label widths, word orders, repeated words, punctuation and whitespace, pasted lists up to 1,442 lines, stopwords, competing rules, `ctr` values, the spans stage on its own, direction flags and context words, checked against an oracle that tries every choice of occurrences and against the 541 cases filed with #11 and the 935 filed with #12 |
| `concurrency` | 24 engines on 8 threads through the public C API |
| `native`, `text`, `embedding` | Ontology loading, caller metadata, Unicode and URI handling, the embedding example |
| `cli_json`, `cli_jsonf`, `cli_stopwatch` | The one-shot CLI's output formats |
| `performance` | Ceilings on load, first parse, parse and peak memory, run in optimized builds without sanitizers |

[Validation results](https://github.com/craigtrim/mutatoc/blob/master/tests/validation.json) record the executed checks. The [implementation map](https://github.com/craigtrim/mutatoc/blob/master/docs/implementation-map.json) accounts for Mutato's 91 source modules. [Performance](performance.md) covers the benchmark and earlier optimizations.

## Evidence

- The gold corpus (`tests/fixtures/gold/corpus.json`) holds 11 hand-written paragraphs and documents, at least one for each of the seven distinct fixture ontologies, with 132 expected entities. Each text was audited against its ontology's vocabulary read from the OWL source with an independent parser, and its expectations were frozen before the engine first ran on it. On that first run 10 of the 11 texts matched exactly. The eleventh had a span rule the audit had not modelled (`abdominal_wound+penetrating_wound` on `Penetrating_Abdominal_Wound`), and the fixture records the original expectation, the correction and its derivation. The `gold` suite requires the same entities in the same order, with equal canonical form, match type, NER label and covered text.
- Mutato's 961 upstream tests were recorded request by request against 0.2.3 (3,930 requests in 252 engine sessions, `tests/fixtures/upstream/trace.json`). The `upstream` suite replays every session in a fresh engine and compares each response structurally, and each parse on its entities and rendered text. The real 961 tests also passed against 0.3.0 before the harness was retired.
- The matching corpus contains 393 cases. The `parity` suite compares complete prepared-token output and, for the same texts, the entities and rendered text of a raw parse.
- The public finder corpus contains 1,659 calls across 140 methods on four interfaces.
- Optional matching stages and live matching have 81 reference cases, and exact windows have 266. Collection loading, external synonyms and blank-node separation have explicit regressions.
- All 313 retained W3C RDF 1.1 Turtle tests run against expected graphs converted once from the W3C result files. All 19 ontology graphs keep their triple counts and a fingerprint that no blank-node relabeling can change; both were recorded after a final isomorphism check against an independent parser.
- The `sources` suite runs 5,804 checks across native JSON and JSONL readers: 328 graph round-trips from TTL, 3,318 finder comparisons, 1,572 raw/prepared matching comparisons, and checks for optional stages, mixed collections, malformed input, and atomic reloads. Both formats retain the same graph terms, prefix bindings, triple order, and matching views.
- URI resolution, Unicode case conversion, whitespace, embedded NUL values, typed literals, caller metadata and concurrent use of the public C API have additional checks.

Unordered extraction and query collections are compared as multisets, preserving multiplicity and scalar types. Token arrays and histories are compared in order. Graph results are compared modulo blank-node identifiers.

## Language adaptations and source defects

Comma-bearing labels and synonyms stay whole by default ([#15](https://github.com/craigtrim/mutatoc/issues/15)). The reference split every literal at commas, which let `Crime` match a class named `Equality, Crime, And Justice`. The opt-in `comma_lists` option retains the packed-list convention, preserves commas inside numbers, and expands each item of a packed `+` list into its own span rule. Existing prepared snapshots keep their stored views. [Input formats](input-formats.md#comma-literals-and-packed-lists) covers migration.

The econ and acanames parity snapshots have regenerated synonym and span views, with unrelated historical views and ordering retained. The one econ case that recognized `global` solely as a comma piece now leaves that word unmatched; its original expected token is retained in the fixture. Medic-copilot parity snapshots keep their packed synonym lists. Upstream replay explicitly enables `comma_lists` and applies six recorded span-bucket corrections from `tests/fixtures/upstream/comma-lists.json`, checking each original value before applying its replacement. The frozen upstream trace remains unchanged.

Version 0.3.0 tokenizes raw text natively. Across 12,386 distinct texts, its tokens match 0.2.3's exactly in all but 91; the rest keep words that 0.2.3 split apart (`cannot`, `id`, `im`, `gotta`, `5G`, `9am`, `500mg`, `y'all`, `guv'nor's`) or remove whitespace that 0.2.3 glued onto a token after an apostrophe. Parsing those texts against seven ontologies (87,067 parses) found every entity 0.2.3 found, with the same canonical form, type and label; three spans moved where 0.2.3's offsets had absorbed that stray whitespace. A `hierarchy` match with no ontology label now has a null `ner` rather than a label taken from a caller token field; no recorded Mutato case produced one. The optional stage that grouped tokens by a statistical model's entity labels, which Mutato disabled in 2022, is gone, and so are the two query stubs for those labels; [the changelog](https://github.com/craigtrim/mutatoc/blob/master/CHANGELOG.md) lists every removed operation.

Mutato's span distance check (`SpanDistanceCheck`) compares the positions of only the first and last words in a rule's content, and reads each word at its last occurrence in the text. A rule of three or more words could therefore match with its other words anywhere, and a repeated word could hide a valid group of words earlier in the text. The C runtime holds every word to the distance and uses the occurrences that lie closest together ([#9](https://github.com/craigtrim/mutatoc/issues/9)); no recorded Mutato case relied on the old check. The direction flags keep Mutato's meaning. Since [#12](https://github.com/craigtrim/mutatoc/issues/12), whitespace-only tokens take no position in span distance, as they never interrupt an exact phrase; Mutato counted them (see [Span rules](protocol.md#span-rules)).

Version 0.2.2 corrects dotted-abbreviation tokenization: `U.S. Virgin Islands` retains its periods and can match the complete ontology synonym. Exact matching derives its maximum window from the ontology instead of imposing a ten-token cutoff. Whitespace-only tokens do not interrupt an exact phrase, but remain in the match history. Longest-match priority and leftmost tie selection are preserved; punctuation is never skipped as whitespace. The punctuation regression suite uses authored ontology fragments, positive and negative matches, long-window boundaries, and independent source-preservation assertions.

Multiple ontologies retain their declared order, merge arrays without duplicate values, and retain every ontology name in swap history. Blank nodes are scoped per document. The original multi-finder attempts to hash dictionaries in some merge paths and raises `TypeError`; the C implementation performs the intended merge. Ambiguous canonical choices use declared source order.

The original optional hierarchy service loops forever when it finds candidates but makes no swap. The C service returns the unchanged list. The stage oracle records this defect by detecting the stationary source transition; it does not describe a timed-out source call as a passing comparison.

`FindOntologyJSON.equivalents` calls a missing helper in the reference. The matching facade returns an explicit error. `interface: "ask_json"` retrieves the stored equivalents view, and `interface: "data"` performs the working entity query. `infer_by_requires` remains explicitly unavailable, as it is in Mutato.

The source blacklist switch checks integer keys against a string-keyed mapping and has no matching effect. That behavior is retained. Mutato's cached `_and_self` methods mutate shared lists; C requests return independent JSON values and avoid cross-call cache contamination. Fixed reverse-view quirks are retained where the reference returns a forward map.

Typed literals of the common XSD types are normalized as Mutato normalized them. Since 0.3.0, date, time, duration, `base64Binary` and `rdf:XMLLiteral` literals keep their lexical form (`2020-01-01T01:02:03Z` stays as written instead of becoming `2020-01-01T01:02:03+00:00`), and loading them no longer requires any configuration.

The native runtime uses explicit snapshots instead of Mutato's pickle/joblib caches or implicit cache directories. `--snapshot` writes the generated MDA object; `load` can restore it. `--force-cache` rebuilds from OWL. Callable arguments to `transitive` become method names. Empty results retain Mutato's null conventions. Mutato's `swap_input_text("")` returns None; the C protocol returns empty tokens.

Input paths are explicit UTF-8 paths. The legacy namespace argument remains ineffective, matching the disabled namespace binding in the reference; source namespace declarations determine IRIs. External synonyms use the source filename plus `.txt`, including the original `<ontology>.owl.txt` convention.

## Resource and platform boundaries

Supported ontology serializations are Turtle-encoded OWL and the `mutatoc/1` JSON/JSONL record schema. All readers populate the same runtime graph directly. The structured schema includes arbitrary triples and complete RDF terms so it can retain every graph fact the Turtle reader accepts. Prepared MDA snapshots remain supported separately. YAML, OWL/XML and RDF/XML readers are not included. This engine preserves Mutato's matching rules; it does not add a general OWL reasoner.

Input files and protocol messages are bounded at 256 MiB. Turtle/blank-node recursion is bounded at 128 levels; hierarchy traversal and matching have explicit cycle/work guards. Resource-limit failures return diagnostics rather than silently truncated results. No finite corpus proves equivalence for every possible ontology or sentence; the recorded results describe the exercised contract.

Windows MSVC and GCC, and Linux GCC with address/undefined-behavior sanitizers, are validated. GitHub Actions builds and tests Windows MSVC, Linux GCC and the Linux sanitizer configuration on every push, and test fixtures keep their exact bytes so both platforms parse the same graphs. macOS and other architectures have not been validated. The packaged Windows distribution is x64.
