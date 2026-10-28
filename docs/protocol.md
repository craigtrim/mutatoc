# JSON and C integration

Each request is one JSON object. The CLI's `--serve` mode accepts newline-delimited UTF-8 requests. One engine handles requests sequentially. Everything runs in process: there are no worker processes, models or interpreters to configure. Result tokens preserve arbitrary supplied fields, including fields nested in swap history.

| Operation | Inputs | Result |
| --- | --- | --- |
| `version` | none | library version |
| `load` | `path`, inline `content` or `turtle`, `snapshot`, `paths`, or `sources`; optional `format`, `name`, `base`, `class_based`, `distance`, `interface`, `graph_only`, `comma_lists` | loaded ontology metadata |
| `update` | `replace` mapping entity IRIs to Turtle strings, and/or `remove` array of entity IRIs | accepted replacement and removal counts |
| `snapshot` | none | generated MDA object |
| `read_rdf` | `content` and optional `format`, or `turtle`; optional `base` | typed RDF triples without changing the loaded ontology |
| `triples` | none | parsed RDF graph; unavailable after loading a prepared MDA snapshot alone |
| `schema` | none | ontology schema |
| `detect_schema` | `content` and optional `format`, or `turtle`; optional `base` | schema without changing loaded ontology |
| `query` | `method`, `args`, optional `kwargs`, `interface` | finder/query result |
| `parse` | `text`, optional `ctr`, `line_breaks` | canonical `text` and complete matched `tokens` |
| `transform_tokens` | `stage`, `tokens`, optional `line_breaks` | one optional service: `exact`, `spans`, `hierarchy`, or `augment` |
| `parse_tokens` | `tokens`, optional `ctr`, `line_breaks` | matched tokens and canonical text, without tokenizing |
| `tokenize` | `text` | the tokens `parse` would match |
| `generate_synonyms` | `data`, optional `reverse` | synonym view |
| `generate_lookup` | `data` | lookup view |
| `generate_spans` | `data`, optional `distance`, `plus_only`, `comma_lists` | span rules |

The `query` interface defaults to cached JSON finder behavior. `owl` and `data` select direct graph and live finder semantics when an RDF graph is loaded. `ask_json` selects the low-level stored JSON views. Unknown operations and methods return errors. The complete exercised method list is recorded in `tests/fixtures/api/queries.json`.

`load` constructs the new graph and snapshot before replacing the existing ontology. Failed loads retain the previous ontology. Eligible large loads build independent indexes with at most two additional native threads, which finish before `load` returns.

`comma_lists` is a boolean, false by default on every load. Whole literals, including their commas, supply synonyms and span rules. Set it to true for sources that pack lists into literals; commas between numeric characters remain intact. Collections inherit it per source, with explicit source values taking precedence. Deferred `graph_only` extraction retains it, and failed loads preserve the previous setting. It does not rewrite prepared snapshots. See [comma literals and packed lists](input-formats.md#comma-literals-and-packed-lists) for migration examples.

`format` accepts `auto` (default), `turtle`/`ttl`, `json`, `jsonl`, or `snapshot`. Inline `content` is a source-text string. JSON arrays and JSONL ontology records load directly into the same graph as Turtle; prepared MDA snapshots remain distinct. See [Ontology input formats](input-formats.md) for the schema and examples.

## Update entities

`update` applies a batch to one loaded RDF graph. Each `replace` value contains all statements for its subject, including statements the caller wants to retain. An empty string removes that subject's statements. `remove` also removes subject statements; it leaves incoming references from other subjects intact. Removing an absent subject succeeds.

```json
{"op":"update","replace":{"https://example.org/#school42":"<https://example.org/#school42> rdfs:label \"Regional Science Academy\" ."},"remove":["https://example.org/#school17"]}
```

The response is `{"ok":true,"result":{"replaced":1,"removed":1}}`. Counts describe accepted request entries. Empty replacement and removal containers are a successful no-op.

Entity identifiers must be absolute IRIs. Each Turtle fragment inherits the loaded graph's prefixes and uses its entity IRI as its base. Prefix declarations inside a fragment apply to that fragment. Statements must use the specified entity IRI as subject and cannot contain blank nodes. A subject may appear only once across the request. Prepared snapshots and source collections do not support `update`.

Every fragment is checked before the batch changes the engine. Invalid Turtle, mismatched subjects, duplicate subjects, and extraction errors leave the graph and matching results unchanged. Replacements occupy the original subject's position; new subjects append in request order. The existing load options remain in effect.

Literal vocabulary replacements can reuse the loaded graph and matching index. Mutatoc maintains references to shared lookup forms, tracks substring dependencies, and patches affected synonym and span buckets. Changing a label's word count also updates lookup ordering and token aliases. The work follows the changed vocabulary and its affected dependencies.

Structural changes, additions, and removals rebuild the derived views transactionally. Mutatoc also uses that path for blank-node graphs, explicit `+` literals, comma-list mode, external synonym files, colliding canonical names, and replacements that change the subject's predicate layout. Both paths produce the results of loading the resulting graph. No consumer changes are needed for faster `load`; a consumer uses incremental edits by sending `update`.

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
- `x` and `y` are code point offsets into the input: `x` is where the token starts and `y` is where it ends without its trailing whitespace, so a whitespace token has `y` equal to `x`. Invisible characters are not whitespace, so they count toward `y`.
- `normal` is the text lowercased, with every hyphen and dash folded to `-`, every apostrophe and single quote folded to `'`, and every double quote, including the pairs ``` `` ``` and `´´`, folded to `"`. Accents and invisible characters are then dropped, so `Café` has the `normal` `cafe` (see [Normalization](#normalization)). Matching reads `normal` for word forms; optional line boundaries also inspect whitespace token `text`.
- `id` is `<hash>#<index>`: the MurmurHash64A (seed 1) of the token's text without its trailing space, and its position in the result. Treat ids as opaque identifiers scoped to one tokenization result; they are not stable across tokenizer changes.

Punctuation becomes its own token, except a period or comma inside a number, an apostrophe inside a word, and an ampersand between letters. A period or comma that ends a number is split off (`2020.` becomes `2020` and `.`), and so are underscores that open or close a word (`_name_`). An apostrophe that ends a word is split off as a closing quote, except after a plural `s` (`dogs'`). A pair of backticks or acute accents is one token. Every character that folds to `'` behaves as `'` in these rules, so `Driver’s` is one token just as `Driver's` is. Contractions stay whole (`don't`, `can't`, `y'all`, `O'Brien`), and so do numbers written with units (`5G`, `9am`, `500mg`) and words such as `cannot`. Abbreviations split at their periods (`dept.` becomes `dept` and `.`). A combining mark or default ignorable code point, including the Hangul fillers that are also letters, never splits a word or a run of whitespace: it stays in the token it follows, and one at the start of the text joins the token after it. `cafe` followed by U+0301 is one token, `da` U+00AD `ta` is one token, and `data` U+200B `science` is the one word `datascience`. The punctuation rules above look past these characters, so `2020.` followed by U+00AD still splits off its period and `A&` U+00AD `B` stays one token. A token made only of whitespace and invisible characters has an empty `normal` and counts as whitespace in matching. `tests/test_tokenize.c` pins these cases, and `tests/test_token_fidelity.c` holds every token and entity to the input.

A matched entity is a new token `{id, x, y, ner, text, normal, swaps}` whose `normal` is the canonical form and whose `swaps.tokens` holds the original tokens. For `parse`, its `text` is the input from its `x` to its `y`; for `parse_tokens`, it is its tokens' trimmed texts joined by single spaces. `ner` comes from the ontology; a `hierarchy` match with no ontology label has a null `ner`.

## Normalization

<!-- craigtrim/mutatoc#25 -->

Matching compares folded text, so a word matches whether or not it carries accents or invisible characters. The fold is the same on both sides: input tokens get it in `normal`, and labels, synonyms, span rules and hierarchy entity names get it when the match index is built. Folding a word:

1. lowercases it as before, so `ẞ` becomes `ß` and `İ` becomes `i` with a combining dot above;
2. applies canonical decomposition, so a precomposed `é` and `e` followed by U+0301 become the same;
3. drops every combining mark (general categories Mn, Mc and Me) and every default ignorable code point, including the reserved code points Unicode already marks default ignorable. Hangul syllables stay whole.

A span rule word that folds away, such as a lone soft hyphen between two words of a label, is no longer required, so `alpha` U+00AD `beta` matches as `alpha beta` does. Words that fold alike are the same word, so `café cafe` matches as `cafe cafe` does. A span rule that dropping its invisible-only words leaves with a single word is removed, since the exact stage already matches that word through the folded synonym. A span rule word that was already empty stays required and never matches, as before.

Span rules can turn one token back and forth: folding can let a rule match the entity another rule made, through its words or its context, and rules written without accents can do the same. 0.7.0 kept applying such spans without end. The spans stage now keeps the sequence of forms it has had since its last span over several tokens. When the span it picks covers one token that a span already covers, and would bring back a sequence the stage has already had, that token takes no more spans over it alone, in this sweep or a later one, and the stage picks again, trying the word's next occurrence. A stage that finishes never brings a sequence back, so this changes no result that finished before.

A token whose history holds 256 spans along one branch takes no more spans over it alone; exact and hierarchy matches in that history do not count. Each span is three levels of JSON and a request accepts at most 1,000, so the spans leave room for those matches in a result that goes back in through `parse_tokens`. The limit also bounds rules that keep a token changing without bringing a sequence back: single-word rules with context words can count in binary, so a counter of 12 bits would otherwise take 24,570 spans before its sequence repeats. Results differ from 0.7.0 only where one token went past 256 such spans; for a seven-bit counter 0.7.0 returned a history 509 spans deep, which its own `parse_tokens` rejected as invalid JSON.

With `tea+tea` on a class `cafe` and `café+café` on a class `tea`, the text `tea` ends on `cafe` and the text `cafe` ends on `tea`. A token that no span covers yet always takes its first span, so `cafe+cafe` on a class `café` matches both words of `cafe x cafe`.

| Label | Text | Match |
| --- | --- | --- |
| `Café Crème` | `cafe creme`, `CAFÉ CRÈME`, `cafe` U+0301 ` cre` U+0300 `me` | yes |
| `cafe` | `I went to café  san martin` | yes, reporting `café` at 10 to 14 |
| `İstanbul` | `istanbul`, `ISTANBUL` | yes |
| `Straße` | `STRAẞE` | yes |
| `Straße` | `STRASSE` | no: `ß` is not folded to `ss` |
| `data science` | `da` U+00AD `ta science`, `data` U+200B ` science` | yes |
| `data science` | `data` U+200B `science` | no: the invisible character joins the one word `datascience` |
| `Café Crème` | `ｃａｆé ｃｒèｍｅ` (fullwidth) | no: fullwidth letters are not folded |

Folding leaves alone dotless `ı`, `ß`, fullwidth and other compatibility forms, and punctuation between words ([#16](https://github.com/craigtrim/mutatoc/issues/16)).

A match reports the input exactly as sent: `text`, `x` and `y` never change, and only `normal` shows the folded form matching compared. Prepared snapshots keep their stored views. Their keys are folded when the match index is built, so a snapshot prepared before folding matches exactly as a fresh load of the same ontology does. Tokens supplied to `parse_tokens` and `transform_tokens` are compared folded as well, including a token whose `normal` folds to nothing, which counts as whitespace. Tokens prepared before folding therefore still match, and their `normal` comes back as sent.

When two entries of one ontology fold to the same key, such as `cafe` and `café`, a lookup word stored in exactly the folded form wins, and otherwise the first entry does. Span rules whose keys fold alike apply in source order, and a hierarchy match takes the first entity name that folds to it.

The Unicode data comes from the vendored utf8proc 2.12.0, which implements Unicode 18.0.0. `tests/test_normalization.c` runs Unicode's `NormalizationTest.txt` against it and checks the fold of every code point against `UnicodeData.txt` and `DerivedCoreProperties.txt`. It also crosses accent forms, case and invisible characters through fresh, live, updated and 0.7.0-snapshot engines, `parse_tokens` and the exact stage.

## Line boundaries

<!-- craigtrim/mutatoc#19 -->

`parse`, `parse_tokens` and `transform_tokens` accept `line_breaks: "ignore"` (the default) or `line_breaks: "boundary"`. The choice applies only to that request and is not stored in the engine or snapshot. Other strings and non-string values, including null, return input error 2.

```json
{"op":"parse","text":"survey\nglacier","line_breaks":"boundary"}
```

With the label `Glacier Survey` loaded, the default mode matches this text as a span. Boundary mode leaves the two words unmatched. A same-line `survey glacier` still matches, including when an earlier candidate would cross a break.

Boundary mode prevents exact, span and hierarchy windows from crossing a whitespace-only token containing LF (U+000A), CR (U+000D), VT (U+000B), FF (U+000C), NEL (U+0085), LINE SEPARATOR (U+2028) or PARAGRAPH SEPARATOR (U+2029). CRLF and runs containing blank lines also block matching. Spaces, tabs and other non-breaking whitespace keep their existing behavior. Span distance still counts only non-whitespace tokens; context requirements still search the whole document. The `augment` stage accepts and validates the option, but annotation does not create matching windows.

Tokenization, source text, code point offsets and swap history are unchanged. For caller-supplied tokens, a boundary token must have empty `normal`, contain only whitespace and invisible characters in `text` (see [Normalization](#normalization)), and have no `swaps` field. Supplied tokens remain atomic: embedded breaks in a visible token, a nonempty `normal`, or existing swap history do not cause retokenization.

## Span rules

<!-- craigtrim/mutatoc#9, craigtrim/mutatoc#11, craigtrim/mutatoc#12 -->

A span rule matches the words of a multiword label when they appear near each other in any order rather than as the exact phrase. The snapshot's `spans` view keys each rule by the label's first word and lists the label's other words, without stopwords, as `content`. A rule matches when the text holds the key and every content word, and one occurrence of each can be chosen so that the first and last chosen positions are at most `distance` apart. The distance is 4 unless `load` or `generate_spans` sets another value. The `spans` entity covers the tokens from the first chosen occurrence to the last.

- Every word of the rule is held to the distance, not only two of them. A rule whose key and content words, after stopwords are dropped, number more than `distance + 1` can therefore match only as its exact phrase: `Office of Tidal Records` needs three words, not four.
- Positions count every token except whitespace. Punctuation takes a position, while a run of whitespace beyond the single space after a word (a second space, a tab or a line break) takes none, just as it never interrupts an exact phrase in the default `ignore` mode. In `marine, pottery, workshop` the first and last words are four positions apart; in `marine pottery workshop` they are two, and they stay two when tabs or line breaks replace the spaces. A span still covers the whitespace between its words, so its `text`, `x` and `y` are those of the input. An entity matched exactly earlier in the parse is one position.
- In the default `ignore` mode, words at the end of one line and the start of the next can match because line breaks take no position. Set `line_breaks: "boundary"` to prevent windows crossing mandatory line breaks while retaining document-wide context.
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
