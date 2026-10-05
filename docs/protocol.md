# JSON and C integration

Each request is one JSON object. The CLI's `--serve` mode accepts newline-delimited UTF-8 requests. One engine handles requests sequentially. Everything runs in process: there are no worker processes, models or interpreters to configure. Result tokens preserve arbitrary supplied fields, including fields nested in swap history.

| Operation | Inputs | Result |
| --- | --- | --- |
| `version` | none | library version |
| `load` | `path`, inline `content` or `turtle`, `snapshot`, `paths`, or `sources`; optional `format`, `name`, `base`, `class_based`, `distance`, `interface`, `graph_only` | loaded ontology metadata |
| `snapshot` | none | generated MDA object |
| `read_rdf` | `content` and optional `format`, or `turtle`; optional `base` | typed RDF triples without changing the loaded ontology |
| `triples` | none | parsed RDF graph; unavailable after loading a prepared MDA snapshot alone |
| `schema` | none | ontology schema |
| `detect_schema` | `content` and optional `format`, or `turtle`; optional `base` | schema without changing loaded ontology |
| `query` | `method`, `args`, optional `kwargs`, `interface` | finder/query result |
| `parse` | `text`, optional `ctr` | canonical `text` and complete matched `tokens` |
| `transform_tokens` | `stage`, `tokens` | one optional service: `exact`, `spans`, `hierarchy`, or `augment` |
| `parse_tokens` | `tokens`, optional `ctr` | matched tokens and canonical text, without tokenizing |
| `tokenize` | `text` | the tokens `parse` would match |
| `generate_synonyms` | `data`, optional `reverse` | synonym view |
| `generate_lookup` | `data` | lookup view |
| `generate_spans` | `data`, optional `distance`, `plus_only` | span rules |

The `query` interface defaults to cached JSON finder behavior. `owl` and `data` select direct graph and live finder semantics when an RDF graph is loaded. `ask_json` selects the low-level stored JSON views. Unknown operations and methods return errors. The complete exercised method list is recorded in `tests/fixtures/api/queries.json`.

`load` constructs the new graph and snapshot before replacing the existing ontology. Failed loads retain the previous ontology.

`format` accepts `auto` (default), `turtle`/`ttl`, `json`, `jsonl`, or `snapshot`. Inline `content` is a source-text string. JSON arrays and JSONL ontology records load directly into the same graph as Turtle; prepared MDA snapshots remain distinct. See [Ontology input formats](input-formats.md) for the schema and examples.

## Load TTL or JSON

Load a TTL source:

```json
{"op":"load","path":"examples/animals.ttl","format":"ttl"}
```

Or load the equivalent JSON source:

```json
{"op":"load","path":"examples/animals.json","format":"json"}
```

Both are ordinary ontology loads. Omitting `format` enables content detection. `read_rdf` and `detect_schema` accept either format through an inline `content` string, and `triples` returns the complete graph after either source load.

The `interface` option selects query and matching behavior independently of the source format. `owl` and `data` queries work with JSON sources as well as TTL sources. A JSON request or response is also independent of the ontology's format: the protocol carries both TTL and JSON source loads.

A prepared MDA snapshot stores extracted matching views. Loading one does not restore an RDF graph. Use TTL or JSON ontology records when the caller needs direct graph queries as well as matching.

## Raw-text tokens

<!-- craigtrim/mutatoc#7 -->

`parse` and `tokenize` split plain text natively, as sent. A consumer that wants spaced apostrophes rejoined, or contractions and abbreviations expanded, does that before sending the text (see [Input text](index.md#input-text)). Each token is `{id, text, x, y, normal}`:

- `text` is a slice of the input, and concatenating every token's `text` reproduces the input. A word keeps the one space that follows it. Any other run of whitespace (tabs, line breaks, further spaces) is its own token, whose `normal` is empty.
- `x` and `y` are code point offsets into the input: `x` is where the token starts and `y` is where it ends without its trailing whitespace, so a whitespace token has `y` equal to `x`.
- `normal` is the text lowercased, with every hyphen and dash folded to `-`, every apostrophe and single quote folded to `'`, and every double quote, including the pairs ``` `` ``` and `´´`, folded to `"`. Matching reads only `normal`.
- `id` is `<hash>#<index>`: the MurmurHash64A (seed 1) of the token's text without its trailing space, and its position in the result. Treat ids as opaque identifiers scoped to one tokenization result; they are not stable across tokenizer changes.

Punctuation becomes its own token, except a period or comma inside a number, an apostrophe inside a word, and an ampersand between letters. A period or comma that ends a number is split off (`2020.` becomes `2020` and `.`), and so are underscores that open or close a word (`_name_`). An apostrophe that ends a word is split off as a closing quote, except after a plural `s` (`dogs'`). A pair of backticks or acute accents is one token. Every character that folds to `'` behaves as `'` in these rules, so `Driver’s` is one token just as `Driver's` is. Contractions stay whole (`don't`, `can't`, `y'all`, `O'Brien`), and so do numbers written with units (`5G`, `9am`, `500mg`) and words such as `cannot`. Abbreviations split at their periods (`dept.` becomes `dept` and `.`). `tests/test_tokenize.c` pins these cases, and `tests/test_token_fidelity.c` holds every token and entity to the input.

A matched entity is a new token `{id, x, y, ner, text, normal, swaps}` whose `normal` is the canonical form and whose `swaps.tokens` holds the original tokens. For `parse`, its `text` is the input from its `x` to its `y`; for `parse_tokens`, it is its tokens' trimmed texts joined by single spaces. `ner` comes from the ontology; a `hierarchy` match with no ontology label has a null `ner`.

## Span rules

<!-- craigtrim/mutatoc#9, craigtrim/mutatoc#11, craigtrim/mutatoc#12 -->

A span rule matches the words of a multiword label when they appear near each other in any order rather than as the exact phrase. The snapshot's `spans` view keys each rule by the label's first word and lists the label's other words, without stopwords, as `content`. A rule matches when the text holds the key and every content word, and one occurrence of each can be chosen so that the first and last chosen positions are at most `distance` apart. The distance is 4 unless `load` or `generate_spans` sets another value. The `spans` entity covers the tokens from the first chosen occurrence to the last.

- Every word of the rule is held to the distance, not only two of them. A rule whose key and content words, after stopwords are dropped, number more than `distance + 1` can therefore match only as its exact phrase: `Office of Tidal Records` needs three words, not four.
- Positions count every token except whitespace. Punctuation takes a position, while a run of whitespace beyond the single space after a word (a second space, a tab or a line break) takes none, just as it never interrupts an exact phrase. In `marine, pottery, workshop` the first and last words are four positions apart; in `marine pottery workshop` they are two, and they stay two when tabs or line breaks replace the spaces. A span still covers the whitespace between its words, so its `text`, `x` and `y` are those of the input. An entity matched exactly earlier in the parse is one position.
- Because line breaks take no position, words at the end of one line of a pasted list and the start of the next sit only a count or so apart and can match. Treating a line break as a boundary would be segmentation, which mutatoc does not do; a caller that wants lines matched apart can send each line on its own.
- When a word occurs more than once, the rule uses the occurrences that lie closest together, and the leftmost of equally close sets.
- A stopword the rule leaves out is not required, but it takes a position when the text contains it.
- `context` words must appear somewhere in the text and are not held to the distance.
- `forward` and `reverse` matter only for rules authored into a prepared snapshot, because generated rules set both to true. The key and content are sorted by length and then by bytes; `forward` lets the first of them follow the last in the text, and `reverse` lets it precede the last. The flags order only those two words.

The spans stage applies rules one at a time until none fits. Each round takes the rule whose canonical form has the most underscores, at its tightest window, and collapses that window, so a text gets every span its words allow. A parse runs three sweeps of exact, span and hierarchy matching (the request's `ctr` field changes the count), and the number of sweeps does not limit the spans; `transform_tokens` with `stage: "spans"` runs the same loop. A collapsed match is one position, so a later span may cover an earlier match, as `Psychology Behavioral` covers the exact match `psychology` with the span `behavioral_psychology`. The distance bounds each window in the stream as it stands, so nested spans can cover more of the original text than the distance suggests. A stage that applies 100,000 spans fails with code 4, as exact matching does.

`tests/test_span_distance.c` holds these rules.

## Errors and ownership

Responses are JSON objects. Error code 1 covers I/O and allocation failures, 2 invalid input, 3 RDF syntax, and 4 ontology and matching operations. Error responses contain a message and may include line and column information.

The C library's result buffers belong to the caller. Always use `mc_free`, including for errors returned as JSON. A NULL `mc_request` return indicates allocation failure. `mc_destroy` releases engine state. Independent engines can run concurrently; access to each individual engine must be serialized.

## Ontology collections

`paths` is an ordered array of file paths. `sources` is an ordered array of ordinary load objects, allowing paths, inline Turtle/JSON/JSONL or prepared snapshots with explicit names and per-source formats. Collection loads are atomic. Arrays merge without duplicate values; canonical ties use source order. A merged RDF graph is available only when every source includes an RDF graph. `interface: "data"` selects live matching and external synonyms at the source filename plus `.txt`, such as `animals.owl.txt` or `animals.jsonl.txt`. `class_based` controls extraction independently.

## Graph queries

The `owl` and `data` interfaces answer every finder method from the native graph and indexes. Arbitrary SPARQL is not part of the engine; `triples` returns the whole graph for callers who want to run SPARQL with their own tooling.

`transitive` accepts a query method name through `kwargs.query` or its second positional argument, where Mutato passed a callable. See `compatibility.md` for inherited facade defects and work limits.

For arbitrary RDF graphs, `load` accepts `graph_only: true` on a single source. This defers matching-ontology extraction, so direct OWL graph queries can operate on graphs with cyclic blank nodes or subclass relations. Requesting a snapshot or matching operation then materializes the MDA view and applies its work and cycle guards. Ordinary ontology loads materialize it immediately.

Typed literals of the common XSD types (integers, decimals, doubles, booleans, `hexBinary`, `normalizedString` and `token`) are normalized the way Mutato normalized them. Date, time, duration, `base64Binary` and `rdf:XMLLiteral` values keep their lexical form after Turtle escape decoding.
