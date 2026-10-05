# Matching performance

## Native JSON and JSONL sources

JSON arrays and JSONL feed the same graph builder and matching engine as Turtle. Each JSON record is released after its facts are inserted. The source buffer and runtime graph remain in memory, but there is no full-document JSON tree or intermediate serialization.

On 2026-10-02, a Windows MSVC Release build measured these medians over seven fresh engines for `acanames-20251028`, the largest fixture used by `mutatoc_bench`:

| Measurement | Turtle | JSON | JSONL |
| --- | ---: | ---: | ---: |
| File load, extraction, and matching indexes | 937.1 ms | 1,084.4 ms | 1,067.9 ms |
| First parse, 2,418-character document | 5.7 ms | 5.8 ms | 5.5 ms |
| Repeated parse | 5.34 ms | 5.46 ms | 5.25 ms |
| Peak benchmark process memory, all four ontologies | 67.5 MB | 67.7 MB | 67.9 MB |

The generated JSON fixtures use explicit RDF term objects and ordered facts, preserving every triple and literal attribute. They are more verbose than hand-authored flat entity records. Fixture generation runs in a separate test process before timing; the measured loaders read each supplied file directly. The additional JSON load cost in this run is about 14 to 16%, with similar matching time and peak memory. These are local timing observations, not a guarantee that one syntax is faster on every input; JSONL mainly changes record framing.

[Ontology input formats](input-formats.md#implementation-and-verification) gives the fixture-generation and benchmark commands. The existing Turtle performance gate remains enabled. Cross-format tests separately compare graph order, terms, prefix bindings, snapshots, finder responses, and matching results.

## Native tokenization

Version 0.3.0 tokenizes raw text in process. 0.2.3 sent every raw-text request to a separate worker process running a statistical model, which cost a process start and model load on the first parse, a JSON round trip on every parse (two when the text contained an apostrophe), and the model's own run time.

`tests/bench.c` (the `mutatoc_bench` target) times the public API. For each ontology it loads the OWL file, parses a document of about 2,400 characters built from that ontology's parity texts, then parses it four more times. The figures are medians of five fresh engines, measured on 2026-09-30 on an AMD Ryzen Threadripper 3960X running Windows 10 with GCC Release builds of both versions; 0.2.3 ran its worker with its pinned runtime.

| Ontology | Load, 0.2.3 | Load, 0.3.0 | First parse, 0.2.3 | First parse, 0.3.0 | Parse, 0.2.3 | Parse, 0.3.0 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| animals-test | 3.1 ms | 3.1 ms | 1,925 ms | 22.1 ms | 212 ms | 22.0 ms |
| econ-20160218 | 67.3 ms | 66.6 ms | 1,897 ms | 8.9 ms | 122 ms | 8.8 ms |
| medicopilot | 110 ms | 110 ms | 1,974 ms | 25.7 ms | 248 ms | 25.3 ms |

Peak memory for the benchmark process fell from 74.0 MB to 67.6 MB. 0.2.3 also ran a worker process that peaked at 133 MB, plus a 3 MB launcher, so the whole 0.2.3 footprint was about 211 MB. Loading is unchanged because ontology construction did not change.

The same shift shows up in larger runs driven through `--serve`. Parsing 12,382 texts against seven ontologies plus the 393 parity texts (87,067 parses) took 23 seconds with 0.3.0 and about 25 minutes with 0.2.3. Mutato's 961 upstream tests took 81 seconds against 0.3.0 and 242 seconds against 0.2.3.

## Tokens as input slices

<!-- craigtrim/mutatoc#7 -->

The tokenizer builds each token as a span of the input in one pass. It used to split, rewrite and re-join token texts and fold each one with thirty string replacements ([#7](https://github.com/craigtrim/mutatoc/issues/7)). The same benchmark, run on 2026-10-02 on the same machine with MSVC Release builds of 0.3.1 and the change, gives these parse medians:

| Ontology | Parse, 0.3.1 | Parse, after #7 |
| --- | ---: | ---: |
| animals-test | 21.9 ms | 6.3 ms |
| econ-20160218 | 9.0 ms | 3.4 ms |
| medicopilot | 25.7 ms | 4.9 ms |
| acanames-20251028 | 15.7 ms | 5.5 ms |

Loads and peak memory (67.3 MB) do not change.

## Span windows

<!-- craigtrim/mutatoc#9 -->

The span stage groups the positions of each normal form once per sweep, and for each rule that could outrank the current best it looks for the closest occurrence of every word ([#9](https://github.com/craigtrim/mutatoc/issues/9)). It used to keep only the last position of each form and compare two of them. The same benchmark, run on 2026-10-05 on the same machine with GCC Release builds of 0.5.0 and the change, gives these parse medians:

| Ontology | Parse, 0.5.0 | Parse, after #9 |
| --- | ---: | ---: |
| animals-test | 6.2 ms | 6.3 ms |
| econ-20160218 | 3.4 ms | 3.5 ms |
| medicopilot | 4.3 ms | 4.3 ms |
| acanames-20251028 | 5.5 ms | 5.3 ms |

The differences are within run-to-run noise. Loads and peak memory (68 MB) do not change.

## Spans until none fits

<!-- craigtrim/mutatoc#11 -->

The span stage now applies rules until none fits instead of once per sweep ([#11](https://github.com/craigtrim/mutatoc/issues/11)). Repeating the single pass would rebuild the position map, sort the distinct forms and walk the token list after every span. The stage instead groups the text's forms into integer ids once, keeps the forms that key span rules in byte order, and after each span moves the later positions down and adds the canonical form. The window search takes starts in ascending order and stops at the first window as tight as the rule allows that begins at its own start. Over 45,486 generated parse, `parse_tokens` and spans stage requests on two ontologies at three distances, the indexed loop and the simple repeated pass returned identical responses.

Measured on 2026-10-05 on the same machine with GCC Release builds:

| Workload | 9ab1dd4 (three spans at most) | Repeated single pass | Indexed loop |
| --- | ---: | ---: | ---: |
| 1,442 `Survey Glacier` lines, 1,442 spans | 75 ms | 1,030 ms | 120 ms |
| `tests/fixtures/bench/acanames-span-list.txt`, about 700 spans | 110 ms | 3,400 ms | 440 ms |
| The four 2,400-character benchmark documents | 3.4 to 6.2 ms | | 3.3 to 6.0 ms |

The first column does less work, since it stops after three spans. Peak memory in the benchmark rises to 79 MB because it now also holds the parse of the 1,442-line list.

Giving whitespace-only tokens no position ([#12](https://github.com/craigtrim/mutatoc/issues/12)) adds one flag per token to that index and numbers positions over the remaining tokens. Run back to back with the build before it on 2026-10-05, the four benchmark documents parsed in 3.4 to 6.5 ms with and without the change, and the span list in 450 to 466 ms against 465 to 494 ms.

## Performance gate

<!-- craigtrim/mutatoc#2 -->

`mutatoc_bench --check` turns the benchmark into a CTest gate named `performance`. It runs in optimized builds (Release, RelWithDebInfo, MinSizeRel) without sanitizers, and fails when any median passes its ceiling:

| Measurement | Ceiling | Measured when the gate was set |
| --- | ---: | ---: |
| Load: animals-test, econ-20160218 | 500 ms | 3 ms, 68 ms |
| Load: medicopilot | 700 ms | 112 ms |
| Load: acanames-20251028 | 6,000 ms | 929 ms |
| First parse of the 2,400-character document | 300 ms | 9 to 26 ms |
| Warm parse of the same document | 150 ms | 9 to 26 ms |
| Parse of `tests/fixtures/bench/acanames-span-list.txt` ([#11](https://github.com/craigtrim/mutatoc/issues/11)) | 2,000 ms | 435 ms |
| Peak memory of the process | 128 MB | 67 MB |

The ceilings are user-facing limits with room for slow shared CI runners, not tight regression factors. They fail the regressions that matter most here: a return to an out-of-process tokenizer (122 to 248 ms per parse and about 1.9 s for the first one), an accidentally quadratic matcher, or a footprint like 0.2.3's 211 MB. A smaller slowdown, such as a load time that grows fivefold but stays under its ceiling, passes; every run prints its measurements so CI logs show the trend.

## Earlier releases

Version 0.2.1 retains the matching rules and complete token results from 0.2.0. Exact matching uses hash membership and records which token windows can match. After a replacement, it recomputes only windows that contain the replacement. Selection still starts with the longest window and then the leftmost match. Unrelated tokens retain their objects instead of being copied after each replacement. Nested swap histories still own complete copies of their original tokens.

Ontology construction uses temporary hash indexes for object members and span-rule deduplication. These indexes preserve JSON insertion order, canonical precedence and rule ordering. They are discarded when construction finishes.

An initial Windows measurement with 23,836 triples reduced ontology loading from 3,788 ms to 896 ms. Full parsing of a 2,429-character document fell from 6,457 ms to 219 ms. That figure included the release's out-of-process tokenizer, whose startup was measured separately. Timing varies by machine and input. No sentence splitting, match limits or ontology content were removed.

Validation compares complete token results for 266 window-boundary, overlap, nested-replacement, Unicode and repeated-match cases generated from the original Mutato matcher, in `tests/fixtures/api/matching-regressions.json`. All 19 OWL fixtures also produced byte-equivalent serialized snapshots against 0.2.0. The normal CTest and Linux address/undefined-behavior sanitizer suites passed.

### Match index

In 0.2.3, matching reads the ontology through an index built once per loaded view instead of through the JSON view itself. cJSON objects are linked lists, so each lookup in 0.2.2 scanned the object, and the exact stage rebuilt a hash set of every synonym on every request. The per-request cost therefore grew with the ontology: one unmatched token cost 0.11 ms on animals-test and 1.38 ms on the largest fixture ontology.

The index holds the synonym sets for each word count, forward and reverse canonical forms, span rules by key with their content already merged and sorted, the entity set and the NER map. Hashed lookups keep cJSON's first-key-wins behavior for duplicate keys and skip unnamed array members, as direct lookups do. The hierarchy stage computes each token's surface forms once per pass instead of once for every window containing the token, and the spans stage no longer walks the token list by index.

The engine owns one index and builds it at the end of every successful `load`. With `interface: "data"`, that includes building the live view, which 0.2.2 deferred to the first parse. Every path that replaces the snapshot or live view frees the index first, so an index is never used with a view other than the one it was built from. A view that cannot be built during load reports its error from the first parse, as before.

Exact matching also stops extending a window once its text cannot begin any synonym. The index records every synonym prefix that ends before a whitespace character. `norm()` only lowercases and trims, and the final-sigma rule does not look past the space that joins tokens, so a window's text is always such a prefix of every longer window from the same token. Before this, each token built and normalized windows up to the longest synonym length in the ontology.

Construction of the whole-ontology entity, subentity and lookup lists also uses hashed membership instead of scanning the list on each insert. Their contents and order are unchanged.

The index also holds each synonym written with punctuation under the text its tokens produce, so `Well/Health/Physical Education` can match the window `well / health / physical education` ([#5](https://github.com/craigtrim/mutatoc/issues/5)). Building the index runs the tokenizer over those synonyms only; a synonym of plain words skips it. On the largest fixture ontology this adds about 30 ms to a load of about 860 ms, and parse times do not change.

| Measurement on Windows | 0.2.2 | 0.2.3 |
| --- | ---: | ---: |
| econ-20160218, 92 prepared-token requests | 136 ms | 44 ms |
| One unmatched token, any fixture | 0.11 to 1.38 ms | 0.1 to 0.2 ms |
| A 2,429-character document, 439 prepared tokens | 49 ms | 16 ms |

About 11 ms of the last row is JSON transport of the 219 KB token array in each direction.

Token results are unchanged. The CTest suite, including byte-equivalent snapshot parity and 1,665 public API cases, all 961 upstream Mutato tests, and the Linux address/undefined-behavior sanitizer suite with leak detection passed. `tests/test_api.c` adds checks that a reload replaces the index in snapshot and live mode and that malformed view members do not break matching. [The changelog](https://github.com/craigtrim/mutatoc/blob/master/CHANGELOG.md#023) records the 0.2.3 speed comparison with Mutato.
