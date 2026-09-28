# Matching performance

Version 0.2.1 retains the matching rules and complete token results from 0.2.0. Exact matching uses hash membership and records which token windows can match. After a replacement, it recomputes only windows that contain the replacement. Selection still starts with the longest window and then the leftmost match. Unrelated tokens retain their objects instead of being copied after each replacement. Nested swap histories still own complete copies of their original tokens.

Ontology construction uses temporary hash indexes for object members and span-rule deduplication. These indexes preserve JSON insertion order, canonical precedence and rule ordering. They are discarded when construction finishes.

An initial Windows measurement with 23,836 triples reduced ontology loading from 3,788 ms to 896 ms. Full parsing of a 2,429-character document fell from 6,457 ms to 219 ms. This includes the unchanged spaCy pipeline and all annotations; model startup is measured separately. Timing varies by machine and input. No sentence splitting, match limits, model components or ontology content were removed.

Validation compares complete token results for 266 window-boundary, overlap, nested-replacement, Unicode and repeated-match cases generated from the original Python matcher. The corpus and generator are `tests/fixtures/api/matching-regressions.json` and `scripts/generate_matching_regressions.py`. All 19 OWL fixtures also produced byte-equivalent serialized snapshots against 0.2.0. The normal CTest and Linux address/undefined-behavior sanitizer suites passed. Release evidence is recorded in `tests/validation.json`.

## Match index

In 0.2.3, matching reads the ontology through an index built once per loaded view instead of through the JSON view itself. cJSON objects are linked lists, so each lookup in 0.2.2 scanned the object, and the exact stage rebuilt a hash set of every synonym on every request. The per-request cost therefore grew with the ontology: one unmatched token cost 0.11 ms on animals-test and 1.38 ms on courses-20251028.

The index holds the synonym sets for each word count, forward and reverse canonical forms, span rules by key with their content already merged and sorted, the entity set and the NER map. Hashed lookups keep cJSON's first-key-wins behavior for duplicate keys and skip unnamed array members, as direct lookups do. The hierarchy stage computes each token's surface forms once per pass instead of once for every window containing the token, and the spans stage no longer walks the token list by index.

The engine owns one index and builds it at the end of every successful `load`. With `interface: "data"`, that includes building the live view, which 0.2.2 deferred to the first parse. Every path that replaces the snapshot or live view frees the index first, so an index is never used with a view other than the one it was built from. A view that cannot be built during load reports its error from the first parse, as before.

Construction of the whole-ontology entity, subentity and lookup lists also uses hashed membership instead of scanning the list on each insert. Their contents and order are unchanged.

| Measurement on Windows | 0.2.2 | 0.2.3 |
| --- | ---: | ---: |
| courses-20251028 snapshot load | 875 ms | 600 ms |
| courses-20251028 live-mode load plus first match | 1,232 ms | 845 ms |
| courses-20251028, 33 prepared-token requests | 266 ms | 9.0 ms |
| econ-20160218, 92 prepared-token requests | 136 ms | 44 ms |
| One unmatched token, any fixture | 0.11 to 1.38 ms | 0.1 to 0.2 ms |

Token results are unchanged. The CTest suite, including byte-equivalent snapshot parity and 1,665 public API cases, all 961 upstream Mutato tests, and the Linux address/undefined-behavior sanitizer suite with leak detection passed. `tests/test_api.c` adds checks that a reload replaces the index in snapshot and live mode and that malformed view members do not break matching. The [speed comparison with Mutato](mutato-comparison.md) covers the full measurements.
