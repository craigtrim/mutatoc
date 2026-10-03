# Compatibility contract

Reference: Mutato `da6bfa5df80b208a3271e111f2921ad281d0da98`.

The logical port preserves ontology extraction and matching, including the differences between direct OWL queries, the live finder, the JSON finder and the low-level JSON API. The same OWL files and prepared snapshots remain inputs, alongside native [JSON and JSONL ontology sources](input-formats.md). Mutato's class instances and callbacks are represented by an opaque C engine and JSON operations.

## Runtime

Everything runs in process, with no interpreter, model or worker. Since 0.3.0 raw text is tokenized natively (see [the protocol](protocol.md#raw-text-tokens)); tokens carry `id`, `text`, `x`, `y` and `normal`, the fields matching reads. Native TTL and JSON readers build the same graph and indexes, including relative IRI resolution, and every finder query runs natively. Arbitrary SPARQL is not provided; `triples` exports the graph from either input format. JSONL supplies the same ontology records as JSON, one per line.

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

Version 0.3.0 tokenizes raw text natively. Across 12,386 distinct texts, its tokens match 0.2.3's exactly in all but 91; the rest keep words that 0.2.3 split apart (`cannot`, `id`, `im`, `gotta`, `5G`, `9am`, `500mg`, `y'all`, `guv'nor's`) or remove whitespace that 0.2.3 glued onto a token after an apostrophe. Parsing those texts against seven ontologies (87,067 parses) found every entity 0.2.3 found, with the same canonical form, type and label; three spans moved where 0.2.3's offsets had absorbed that stray whitespace. A `hierarchy` match with no ontology label now has a null `ner` rather than a label taken from a caller token field; no recorded Mutato case produced one. The optional stage that grouped tokens by a statistical model's entity labels, which Mutato disabled in 2022, is gone, and so are the two query stubs for those labels; [the changelog](https://github.com/craigtrim/mutatoc/blob/master/CHANGELOG.md) lists every removed operation.

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
