# Changelog

<!-- craigtrim/mutatoc#1, craigtrim/mutatoc#2 -->

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
| courses-20251028 | 38,354 / 641 | 524 / 244 | 76.2 / 12.3 | 1,881 / 194 | 40,182 / 2,471 |

About 1.8 seconds of each mutatoc cold run, and most of its document time, was its spaCy worker; 0.3.0 removes both costs.

## 0.2.2

Fixes dotted-abbreviation matching, preserves literal punctuation, and removes the ten-token exact-match cutoff. Whitespace inside an exact phrase remains in its source history. See [punctuation regression coverage](docs/punctuation.md).

## 0.2.1

Exact matching uses hash membership and recomputes only the token windows a replacement touches; ontology construction uses temporary hash indexes. See [performance](docs/performance.md).
