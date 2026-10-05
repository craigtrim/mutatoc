# Changelog

<!-- craigtrim/mutatoc#1, craigtrim/mutatoc#2, craigtrim/mutatoc#5, craigtrim/mutatoc#7, craigtrim/mutatoc#9 -->

## Unreleased

### Fixed

- A span rule now holds every word of a label within its distance ([#9](https://github.com/craigtrim/mutatoc/issues/9)). For a label of three or more words, the check compared only two of them, picked by word length, so the others could sit anywhere and the match stretched over everything between them: one `spans` match for a three-word label covered 588 lines of a pasted list that never contained the label. A repeated word also counted only at its last occurrence, which could hide a valid group of words earlier in the text, for two-word labels as well. A rule now uses the closest occurrence of each word and requires all of them within the distance. See [Span rules](docs/protocol.md#span-rules).

### Breaking changes

- **Fewer, tighter spans:** a span that relied on an unbounded word no longer matches, and a label with more words than the distance plus one matches only as its exact phrase. A span that a later repeat of a word used to hide now matches. Token positions, the default distance of 4 and the `forward` and `reverse` flags keep their meaning.

## 0.5.0 (2026-10-03)

### Added

- Native JSON and JSONL ontology sources, with flat entity records, namespace declarations, arbitrary predicates, ordered facts, and complete RDF terms. Each reader populates the shared C graph directly and releases each parsed record; loading does not convert the document into Turtle or another intermediate format.
- `load`, `read_rdf`, and `detect_schema` accept inline `content` and explicit `format`; the CLI accepts `--format`. JSON arrays and JSONL can be mixed with Turtle in collections and retain graph queries, live matching, optional stages, external synonyms, and atomic reload behavior. Prepared MDA snapshots remain supported separately.
- Cross-format graph, finder, matching, and integration tests, plus reproducible file-load benchmarks. See [Ontology input formats](docs/input-formats.md) for the schema and examples.

## 0.4.0 (2026-10-02)

### Fixed

- Token text and offsets now match the input exactly ([#7](https://github.com/craigtrim/mutatoc/issues/7)). Every token's `text` is a slice of the input and `x` and `y` are code point offsets into it. They used to count positions in rewritten token texts, so one stray quote, tab or repeated space shifted every offset after it: in `say 'hi' now`, `now` was reported at 7 when it starts at 9. A matched entity's `text` from `parse` is the input from its `x` to its `y`, so `U.S. GOV'T & POL` comes back as written instead of as `U . S . GOV'T & POL`.
- Apostrophes and quotes of any form match alike, as hyphens and dashes already did. `Driver’s Ed`, `Driverʼs Ed`, ``Driver`s Ed`` and `Driver´s Ed` all match the synonym `Driver's Ed`, and a synonym written with a curly apostrophe matches straight-apostrophe input. The output keeps whichever character the input used.
- A closing apostrophe after a word is split off wherever the word ends, including at the end of the input and before a tab or line break, not only before a space.

### Breaking changes

- **Offsets:** `x` and `y` index the input in code points. Callers that read positions after a rewritten token see different values, and token ids change for any token whose text changed.
- **No rewriting:** a lone `'` keeps its glyph in `text` and `normal` instead of becoming `"`. Words keep the space before `)`, `"`, `!` and `?`, and line breaks and repeated spaces stay as sent. `can't` and the other contractions stay one token, and abbreviations such as `dept.` split into `dept` and `.`, wherever they fall; 0.3.1 expanded them only when they ended the input. A consumer that wants them expanded does that before sending the text.
- **Curly possessives:** `dog’s collar` no longer matches `dog`, because `dog’s` is now one token, as `dog's` already was.

## 0.3.1 (2026-10-01)

### Fixed

- A synonym written with punctuation now matches as one entity when the text spells it out ([#5](https://github.com/craigtrim/mutatoc/issues/5)). The tokenizer splits punctuation into its own tokens, so `Well/Health/Physical Education`, `PE:PE`, `Calc (Honors)` and `Math Lab [Remedial]` never matched whole; at most a shorter synonym inside them did. The match index now also holds each such synonym in its tokenized form, which makes spaces around the punctuation irrelevant too. Hyphenated synonyms such as `Computer-Aided Manufacturing` now match as `exact` with a null `ner`, where some matched only through `spans` before. Views, queries and span rules are unchanged.

## 0.3.0 (2026-09-30)

Mutatoc no longer uses spaCy, LingPatLab or Python at build, test or run time ([#1](https://github.com/craigtrim/mutatoc/issues/1)). Raw text is tokenized natively in C, so a parse runs entirely in process. Everything that served general NLP rather than ontology matching is gone.

### Added

- `--jsonf` prints the one-shot result as JSON indented two spaces per level; `--json` stays compact.
- `--stopwatch` prints the total run time of a one-shot run after its output, in milliseconds, seconds or minutes as fits. It writes to stderr, so piped JSON stays valid.
- A gold corpus of 11 hand-written paragraphs and documents with 132 expected entities, written down before the engine ran, checked by the `gold` suite ([#2](https://github.com/craigtrim/mutatoc/issues/2)).
- A `performance` suite that fails when load, first parse, warm parse or peak memory passes its ceiling, run in optimized builds without sanitizers (`mutatoc_bench --check`).

### Breaking changes

- **Operations removed:** `lingpatlab` (segmentation, people and topic extraction, name analysis, phrase filtering, prompt generation, and the text, DTO and dictionary helpers), `configure_spacy`, `configure_sparql`, `spacy_info`, `sparql`, `query` with `method: "adhoc"`, the `spacy` stage of `transform_tokens`, and the `spacy_ner` and `spacy_ner_rev` query stubs. `triples` returns the whole graph for callers who want to run SPARQL with their own tooling.
- **C API removed:** `mc_use_spacy` and `mc_use_sparql`. Engines need no configuration.
- **CLI flags removed:** `--python`, `--spacy-worker`, `--sparql-worker`, `--spacy-model` and `--spacy-timeout`, along with the `MUTATOC_PYTHON`, `MUTATOC_SPACY_WORKER`, `MUTATOC_SPARQL_WORKER` and `MUTATOC_SPACY_MODEL` environment variables.
- **Raw-text tokens** from `parse` and `tokenize` now carry `id`, `text`, `x`, `y` and `normal`, the fields matching reads. `head`, `lemma`, `sentiment`, `pos`, `tag`, `dep`, `ent`, `shape`, `is_alpha`, `is_stop`, `other`, `tense`, `verb_form`, `noun_number`, `is_punct`, `is_wordnet` and `stem` are gone. Ids keep the `<hash>#<index>` shape, but the index no longer skips positions, so id values differ; treat them as opaque. Tokens supplied to `parse_tokens` keep every field, as before.
- **Hierarchy labels:** a `hierarchy` match with no ontology label now has a null `ner` instead of a label read from the first token's `ent` field.
- **Typed literals:** `xsd:date`, `dateTime`, `time`, `gYear`, `gYearMonth`, `duration`, `dayTimeDuration`, `yearMonthDuration`, `base64Binary` and `rdf:XMLLiteral` keep their lexical form. For example, `2020-01-01T01:02:03Z` stays as written where 0.2.3 returned `2020-01-01T01:02:03+00:00`, `PT25H` stays as written where 0.2.3 returned `P1DT1H`, and `Y Q==` stays as written where 0.2.3 returned `YQ==`. Loading these types no longer fails for lack of configuration. The common XSD types normalize as before.
- **Error code 5** (worker failures) no longer occurs.
- **Distribution:** the Windows package no longer contains a Python runtime, model or worker scripts; `scripts/package.cmake` replaces the Python packaging script.

### Tokenization

The native tokenizer reproduces the previous tokens on 12,295 of 12,386 distinct test texts. The other 91 keep whole words that the previous pipeline split: `cannot` (was `can not`), `id` (was `i d`), `im` and `dont` (were `i m` and `do nt`), `gotta` and `gonna` (were `got ta` and `gon na`), numbers with units such as `5G`, `9am` and `500mg` (were split from the number, which kept a `5G` label from matching), and `y'all` and `guv'nor's` (were broken at the apostrophe). They also no longer glue a space onto the token after an apostrophe word, which had shifted later offsets. A period after a non-ASCII numeral such as `Ⅵ.` is now split off like one after `2020.`.

Parsing those texts against seven ontologies (87,067 parses) found every entity 0.2.3 found, with the same canonical form, type and label. Three entity spans moved where 0.2.3's offsets had absorbed stray whitespace.

### Tests

Every check runs in CTest without Python. Mutato's 961 upstream tests were recorded request by request against 0.2.3 and are replayed in C; the real tests also passed against 0.3.0 before their harness was retired. The punctuation, extensions, concurrency and RDF suites were ported to C. The W3C Turtle suite runs against expected graphs converted once from the W3C result files, and the 19 ontology graphs are checked against recorded triple counts and blank-node-independent fingerprints. New suites cover the tokenizer contract, typed literals and caller metadata. Test fixtures now keep their exact bytes on every platform; a Windows clone made before this release still holds converted copies, which `git rm -r --cached -q tests/fixtures tests/w3c-turtle`, `git reset -q` and `git checkout -- tests/fixtures tests/w3c-turtle` replace once.

### Performance

On the same harness, parsing a 2,400-character document dropped from 122 to 248 ms to 9 to 25 ms, and the first parse after loading dropped from about 1.9 seconds to under 26 ms. Peak memory fell from about 211 MB across two processes to 68 MB in one. Load times are unchanged. See [performance](docs/performance.md#native-tokenization).

## 0.2.3

Matching reads the ontology through an index built once per loaded view, so the cost of a matching request no longer grows with ontology size. Token results are unchanged. See [the match index](docs/performance.md#match-index).

Measured on 2026-09-28 against Mutato 1.1.1 (`da6bfa5`) on an AMD Ryzen Threadripper 3960X running Windows 10, with identical output in every comparison. Both sides used Python 3.11.0, spaCy 3.8.2 and `en_core_web_sm` 3.8.0; mutatoc ran through `--serve`. Times are medians in milliseconds, Mutato first and mutatoc 0.2.3 second. "Prepared tokens" is every parity case for the ontology through `parse_tokens`; "document" is a warm parse of about 2,400 characters; "cold" is a new process that loads the model, builds from OWL and parses one phrase.

| Ontology | Build from OWL | Snapshot restore | Prepared tokens | Document | Cold |
| --- | ---: | ---: | ---: | ---: | ---: |
| animals-test | 882 / 8.9 | 365 / 1.5 | 88.3 / 18.2 | 306 / 176 | 2,889 / 1,793 |
| econ-20160218 | 14,616 / 61.8 | 370 / 31.6 | 148 / 48.5 | 228 / 130 | 16,666 / 1,906 |
| medicopilot | 5,542 / 93.0 | 390 / 43.2 | 15.6 / 4.0 | 334 / 211 | 7,623 / 1,978 |

About 1.8 seconds of each mutatoc cold run, and most of its document time, was its spaCy worker; 0.3.0 removes both costs.

## 0.2.2

Fixes dotted-abbreviation matching, preserves literal punctuation, and removes the ten-token exact-match cutoff. Whitespace inside an exact phrase remains in its source history. See [punctuation regression coverage](docs/punctuation.md).

## 0.2.1

Exact matching uses hash membership and recomputes only the token windows a replacement touches; ontology construction uses temporary hash indexes. See [performance](docs/performance.md).
