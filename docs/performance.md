# Matching performance

Version 0.2.1 retains the matching rules and complete token results from 0.2.0. Exact matching uses hash membership and records which token windows can match. After a replacement, it recomputes only windows that contain the replacement. Selection still starts with the longest window and then the leftmost match. Unrelated tokens retain their objects instead of being copied after each replacement. Nested swap histories still own complete copies of their original tokens.

Ontology construction uses temporary hash indexes for object members and span-rule deduplication. These indexes preserve JSON insertion order, canonical precedence and rule ordering. They are discarded when construction finishes.

An initial Windows measurement with 23,836 triples reduced ontology loading from 3,788 ms to 896 ms. Full parsing of a 2,429-character document fell from 6,457 ms to 219 ms. This includes the unchanged spaCy pipeline and all annotations; model startup is measured separately. Timing varies by machine and input. No sentence splitting, match limits, model components or ontology content were removed.

Validation compares complete token results for 266 window-boundary, overlap, nested-replacement, Unicode and repeated-match cases generated from the original Python matcher. The corpus and generator are `tests/fixtures/api/matching-regressions.json` and `scripts/generate_matching_regressions.py`. All 19 OWL fixtures also produced byte-equivalent serialized snapshots against 0.2.0. The normal CTest and Linux address/undefined-behavior sanitizer suites passed. Release evidence is recorded in `tests/validation.json`.
