# JSON and C integration

Each request is one JSON object. The CLI's `--serve` mode accepts newline-delimited UTF-8 requests. One engine handles requests sequentially. Everything runs in process: there are no worker processes, models or interpreters to configure. Result tokens preserve arbitrary supplied fields, including fields nested in swap history.

| Operation | Inputs | Result |
| --- | --- | --- |
| `version` | none | library version |
| `load` | `path`, inline `turtle`, `snapshot`, `paths`, or `sources`; optional `name`, `base`, `class_based`, `distance`, `interface`, `graph_only` | loaded ontology metadata |
| `snapshot` | none | generated MDA object |
| `read_rdf` | `turtle`, optional `base` | typed RDF triples without changing the loaded ontology |
| `triples` | none | parsed RDF graph; unavailable after loading JSON alone |
| `schema` | none | ontology schema |
| `detect_schema` | `turtle` | schema without changing loaded ontology |
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

## Raw-text tokens

`parse` and `tokenize` split plain text natively. Each token is `{id, text, x, y, normal}`:

- `text` is the token as it appears, with a single trailing space when a space follows it. Whitespace other than one space between tokens (tabs, line breaks, repeated spaces) becomes its own token, whose `normal` is empty.
- `x` and `y` are the token's start and end in the stream of token texts; `y` excludes the trailing space.
- `normal` is the lowercased text with typographic hyphens and quotes folded to ASCII.
- `id` is `<hash>#<index>`: the MurmurHash64A (seed 1) of the token's text and its position in the result. Treat ids as opaque identifiers scoped to one tokenization result; they are not stable across tokenizer changes.

Punctuation becomes its own token, except a period or comma inside a number, an apostrophe inside a word, and an ampersand between letters. A period or comma that ends a number is split off (`2020.` becomes `2020` and `.`), and so are underscores that open or close a word (`_name_`). A lone single quote reads as a double quote. Words sit directly against a following `)`, `"`, `!` or `?`. Contractions stay whole (`don't`, `y'all`, `O'Brien`), except for listed expansions such as `can't` to `can not`. Numbers written with units (`5G`, `9am`, `500mg`) and words such as `cannot` also stay whole. `tests/test_tokenize.c` pins these cases.

A matched entity is a new token `{id, x, y, ner, text, normal, swaps}` whose `normal` is the canonical form and whose `swaps.tokens` holds the original tokens. `ner` comes from the ontology; a `hierarchy` match with no ontology label has a null `ner`.

## Errors and ownership

Responses are JSON objects. Error code 1 covers I/O and allocation failures, 2 invalid input, 3 RDF syntax, and 4 ontology and matching operations. Error responses contain a message and may include line and column information.

The C library's result buffers belong to the caller. Always use `mc_free`, including for errors returned as JSON. A NULL `mc_request` return indicates allocation failure. `mc_destroy` releases engine state. Independent engines can run concurrently; access to each individual engine must be serialized.

## Ontology collections

`paths` is an ordered array of file paths. `sources` is an ordered array of ordinary load objects, allowing paths, inline Turtle or prepared snapshots with explicit names. Collection loads are atomic. Arrays merge without duplicate values; canonical ties use source order. A merged RDF graph is available only when every source includes an RDF graph. `interface: "data"` selects live matching and external `<ontology>.owl.txt` synonyms. `class_based` controls extraction independently.

## Graph queries

The `owl` and `data` interfaces answer every finder method from the native graph and indexes. Arbitrary SPARQL is not part of the engine; `triples` returns the whole graph for callers who want to run SPARQL with their own tooling.

`transitive` accepts a query method name through `kwargs.query` or its second positional argument, where Mutato passed a callable. See `compatibility.md` for inherited facade defects and work limits.

For arbitrary RDF graphs, `load` accepts `graph_only: true` on a single source. This defers matching-ontology extraction, so direct OWL graph queries can operate on graphs with cyclic blank nodes or subclass relations. Requesting a snapshot or matching operation then materializes the MDA view and applies its work and cycle guards. Ordinary ontology loads materialize it immediately.

Typed literals of the common XSD types (integers, decimals, doubles, booleans, `hexBinary`, `normalizedString` and `token`) are normalized the way Mutato normalized them. Date, time, duration, `base64Binary` and `rdf:XMLLiteral` values keep their lexical form after Turtle escape decoding.
