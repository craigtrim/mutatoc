# Matching performance

<!-- 0.3.0 section: craigtrim/mutatoc#1 -->

## Native tokenization

Version 0.3.0 tokenizes raw text in process. 0.2.3 sent every raw-text request to a separate worker process running a statistical model, which cost a process start and model load on the first parse, a JSON round trip on every parse (two when the text contained an apostrophe), and the model's own run time.

`tests/bench.c` (the `mutatoc_bench` target) times the public API. For each ontology it loads the OWL file, parses a document of about 2,400 characters built from that ontology's parity texts, then parses it four more times. The figures are medians of five fresh engines, measured on 2026-09-30 on an AMD Ryzen Threadripper 3960X running Windows 10 with GCC Release builds of both versions; 0.2.3 ran its worker with its pinned runtime.

| Ontology | Load, 0.2.3 | Load, 0.3.0 | First parse, 0.2.3 | First parse, 0.3.0 | Parse, 0.2.3 | Parse, 0.3.0 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| animals-test | 3.1 ms | 3.1 ms | 1,925 ms | 22.1 ms | 212 ms | 22.0 ms |
| econ-20160218 | 67.3 ms | 66.6 ms | 1,897 ms | 8.9 ms | 122 ms | 8.8 ms |
| medicopilot | 110 ms | 110 ms | 1,974 ms | 25.7 ms | 248 ms | 25.3 ms |
| courses-20251028 | 886 ms | 861 ms | 1,825 ms | 15.6 ms | 151 ms | 15.9 ms |

Peak memory for the benchmark process fell from 74.0 MB to 67.6 MB. 0.2.3 also ran a worker process that peaked at 133 MB, plus a 3 MB launcher, so the whole 0.2.3 footprint was about 211 MB. Loading is unchanged because ontology construction did not change.

The same shift shows up in larger runs driven through `--serve`. Parsing 12,382 texts against seven ontologies plus the 393 parity texts (87,067 parses) took 23 seconds with 0.3.0 and about 25 minutes with 0.2.3. Mutato's 961 upstream tests took 81 seconds against 0.3.0 and 242 seconds against 0.2.3.

## Earlier releases

Version 0.2.1 retains the matching rules and complete token results from 0.2.0. Exact matching uses hash membership and records which token windows can match. After a replacement, it recomputes only windows that contain the replacement. Selection still starts with the longest window and then the leftmost match. Unrelated tokens retain their objects instead of being copied after each replacement. Nested swap histories still own complete copies of their original tokens.

Ontology construction uses temporary hash indexes for object members and span-rule deduplication. These indexes preserve JSON insertion order, canonical precedence and rule ordering. They are discarded when construction finishes.

An initial Windows measurement with 23,836 triples reduced ontology loading from 3,788 ms to 896 ms. Full parsing of a 2,429-character document fell from 6,457 ms to 219 ms. That figure included the release's out-of-process tokenizer, whose startup was measured separately. Timing varies by machine and input. No sentence splitting, match limits or ontology content were removed.

Validation compares complete token results for 266 window-boundary, overlap, nested-replacement, Unicode and repeated-match cases generated from the original Mutato matcher, in `tests/fixtures/api/matching-regressions.json`. All 19 OWL fixtures also produced byte-equivalent serialized snapshots against 0.2.0. The normal CTest and Linux address/undefined-behavior sanitizer suites passed.

### Match index

In 0.2.3, matching reads the ontology through an index built once per loaded view instead of through the JSON view itself. cJSON objects are linked lists, so each lookup in 0.2.2 scanned the object, and the exact stage rebuilt a hash set of every synonym on every request. The per-request cost therefore grew with the ontology: one unmatched token cost 0.11 ms on animals-test and 1.38 ms on courses-20251028.

The index holds the synonym sets for each word count, forward and reverse canonical forms, span rules by key with their content already merged and sorted, the entity set and the NER map. Hashed lookups keep cJSON's first-key-wins behavior for duplicate keys and skip unnamed array members, as direct lookups do. The hierarchy stage computes each token's surface forms once per pass instead of once for every window containing the token, and the spans stage no longer walks the token list by index.

The engine owns one index and builds it at the end of every successful `load`. With `interface: "data"`, that includes building the live view, which 0.2.2 deferred to the first parse. Every path that replaces the snapshot or live view frees the index first, so an index is never used with a view other than the one it was built from. A view that cannot be built during load reports its error from the first parse, as before.

Exact matching also stops extending a window once its text cannot begin any synonym. The index records every synonym prefix that ends before a whitespace character. `norm()` only lowercases and trims, and the final-sigma rule does not look past the space that joins tokens, so a window's text is always such a prefix of every longer window from the same token. Before this, each token built and normalized windows up to the longest synonym length in the ontology.

Construction of the whole-ontology entity, subentity and lookup lists also uses hashed membership instead of scanning the list on each insert. Their contents and order are unchanged.

| Measurement on Windows | 0.2.2 | 0.2.3 |
| --- | ---: | ---: |
| courses-20251028 snapshot load | 875 ms | 600 ms |
| courses-20251028 live-mode load plus first match | 1,232 ms | 845 ms |
| courses-20251028, 33 prepared-token requests | 266 ms | 9.0 ms |
| econ-20160218, 92 prepared-token requests | 136 ms | 44 ms |
| One unmatched token, any fixture | 0.11 to 1.38 ms | 0.1 to 0.2 ms |
| Axiom courses ontology, 439 prepared tokens (2,429 characters) | 49 ms | 16 ms |

About 11 ms of the last row is JSON transport of the 219 KB token array in each direction.

Token results are unchanged. The CTest suite, including byte-equivalent snapshot parity and 1,665 public API cases, all 961 upstream Mutato tests, and the Linux address/undefined-behavior sanitizer suite with leak detection passed. `tests/test_api.c` adds checks that a reload replaces the index in snapshot and live mode and that malformed view members do not break matching. [The changelog](https://github.com/craigtrim/mutatoc/blob/master/CHANGELOG.md#023) records the 0.2.3 speed comparison with Mutato.
