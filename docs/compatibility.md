# Compatibility contract

Reference: Mutato `da6bfa5df80b208a3271e111f2921ad281d0da98`.

The logical port preserves ontology extraction and matching, including the differences between direct OWL queries, the live finder, the JSON finder and the low-level JSON API. The same OWL files and prepared snapshots remain inputs. Python class instances and callbacks are represented by an opaque C engine and JSON operations.

## Retained dependencies

Raw text passes through the native port of LingPatLab 1.1.1. C performs Graffl tokenization, retokenization decisions, token construction, normalization, the source stemmer, coordinates and WordNet membership. The Python worker calls spaCy 3.8.2 with `en_core_web_sm` 3.8.0, applies the spans requested by C through spaCy's retokenizer, and serializes model attributes. POS/dependency/entity annotations and token hashes continue to come from the pinned model. The model is loaded once per engine. LingPatLab, wordnet-lookup and unicodedata2 are absent from the runtime.

The port also exposes LingPatLab's segmentation, people/topic extraction, text utilities, dictionaries, DTO operations and prompt construction. All 69 source modules are accounted for in [the module map](lingpatlab-map.json). Its 17 upstream tests and 8,287 differential cases pass; [the LingPatLab contract](lingpatlab.md) records source defects and language adaptations.

The C RDF reader parses Turtle, builds and indexes triples, and resolves relative IRIs. Built-in ontology queries and matching do not execute SPARQL. The arbitrary `adhoc` API uses a separate RDFLib 7.1.4 worker. Its row transformations remain in C. The same worker preserves RDFLib's conversions for date/time, duration, base64 and XML literals during ontology loading. Ordinary strings, language literals and primitive numeric values are handled natively. C callers loading the special types must configure this worker; missing configuration is an explicit error.

## Evidence

- All 961 upstream test methods are retained, with source hashes and baseline results. The adapter replaces Mutato objects with C API requests. It imports no Python Mutato implementation. Tests that construct RDF fixtures retain RDFLib in the test harness.
- The matching corpus contains 393 cases. Native tests compare complete prepared tokens. Worker tests compare complete raw preprocessing, final tokens, nested histories and canonical text, without dropping statistical fields or rounding identifiers.
- The public finder corpus contains 1,665 calls across 142 methods on four interfaces.
- Optional matching stages and live matching have 90 additional reference cases. Collection loading, external synonyms and blank-node separation have explicit regressions.
- Arbitrary SPARQL has 315 reference calls, including aggregates, paths, subqueries, result transformations and errors. Additional tests cover typed literals, graph reloads, collections and independence from spaCy.
- All 19 original ontology graphs are compared with RDFLib modulo blank-node names. All 313 retained W3C RDF 1.1 Turtle tests are checked, including negative syntax tests.
- URI resolution, Unicode case conversion, whitespace, embedded NUL values, worker failures and the public C ABI have additional checks.

Unordered extraction/query collections are compared as multisets, preserving multiplicity and scalar types. Token arrays and histories are compared in order. SPARQL `ORDER BY` results are compared in order. A dictionary that overwrites several unordered rows with the same key has no stable source value; those cases verify that the selected value is one of the reference query's candidates. Graph results are compared modulo blank-node identifiers.

## Language adaptations and source defects

Version 0.2.2 corrects dotted-abbreviation tokenization: `U.S. History to 1865` retains its periods and can match the complete ontology synonym. Exact matching derives its maximum window from the ontology instead of imposing a ten-token cutoff. Whitespace-only tokens do not interrupt an exact phrase, but remain in the match history. Longest-match priority and leftmost tie selection are preserved; punctuation is never skipped as whitespace. The punctuation regression suite uses authored ontology fragments, positive and negative matches, long-window boundaries, and independent source-preservation assertions. The original LingPatLab fixtures are retained with explicitly identified Python-reference corrections, described in [the tokenizer contract](lingpatlab.md).

Multiple ontologies retain their declared order, merge arrays without duplicate values, and retain every ontology name in swap history. Blank nodes are scoped per document. The original multi-finder attempts to hash dictionaries in some merge paths and raises `TypeError`; the C implementation performs the intended merge. Ambiguous canonical choices use declared source order.

The original optional hierarchy service loops forever when it finds candidates but makes no swap. The C service returns the unchanged list. The stage oracle records this defect by detecting the stationary source transition; it does not describe a timed-out source call as a passing comparison.

`FindOntologyJSON.equivalents` calls a missing helper in the reference. The matching facade returns an explicit error. `interface: "ask_json"` retrieves the stored equivalents view, and `interface: "data"` performs the working entity query. `infer_by_requires` remains explicitly unavailable, as it is in Python. `DICT_OF_STR2DICT` is also unimplemented in the source and returns an error.

The source blacklist switch checks integer keys against a string-keyed mapping and has no matching effect. That behavior is retained. Cached `_and_self` methods mutate shared Python lists; C requests return independent JSON values and avoid cross-call cache contamination. Fixed reverse-view quirks are retained where the reference returns a forward map.

The native runtime uses explicit snapshots instead of Python pickle/joblib caches or implicit cache directories. `--snapshot` writes the generated MDA object; `load` can restore it. `--force-cache` rebuilds from OWL. Python callable arguments to `transitive` become method names. Untransformed SPARQL SELECT/ASK results use SPARQL Results JSON; CONSTRUCT/DESCRIBE use typed triples. Empty results retain Mutato's null conventions. The Python test adapter preserves `swap_input_text("") -> None`; the C protocol returns empty tokens.

Input paths are explicit UTF-8 paths. Namespace arguments remain ineffective, matching the disabled namespace binding in the reference; Turtle prefix declarations determine IRIs. External synonyms use `<ontology>.owl.txt`, as the source actually does.

## Resource and platform boundaries

The supported input serialization is Turtle-encoded OWL, which is also what the original loader selects. OWL/XML and RDF/XML are not added by this port. This engine preserves Mutato's matching rules; it does not add a general OWL reasoner.

Input files and protocol messages are bounded at 256 MiB. Turtle/blank-node recursion is bounded at 128 levels; hierarchy traversal and matching have explicit cycle/work guards. Resource-limit failures return diagnostics rather than silently truncated results. Workers have configurable startup/request deadlines. No finite corpus proves equivalence for every possible ontology or sentence; the recorded results describe the exercised contract.

Windows MSVC and GCC, and Linux GCC with address/undefined-behavior sanitizers, are validated. macOS and other architectures have not been validated. The packaged Windows distribution is x64.
