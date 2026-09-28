# JSON and C integration

Each request is one JSON object. The CLI's `--serve` mode accepts newline-delimited UTF-8 requests. One engine handles requests sequentially. Result tokens preserve arbitrary supplied fields, including fields nested in swap history.

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
| `transform_tokens` | `stage`, `tokens` | one optional service: `exact`, `spans`, `hierarchy`, `spacy`, or `augment` |
| `parse_tokens` | `tokens`, optional `ctr` | matched tokens and canonical text, without preprocessing |
| `tokenize` | `text` | complete original preprocessing token objects |
| `lingpatlab` | `method` and its named inputs | native linguistic API; see [LingPatLab operations](lingpatlab.md) |
| `configure_spacy` | `python`, `worker`, optional `model`, `timeout_ms` | true when configuration is accepted |
| `configure_sparql` | `python`, `worker`, optional `timeout_ms` | configure RDFLib query/literal compatibility |
| `sparql` | `query`, optional `result_type`, `to_lowercase`, `reverse` | arbitrary query result |
| `spacy_info` | none | worker PID, protocol, Python/spaCy versions, native preprocessing compatibility version, model and pipeline |
| `generate_synonyms` | `data`, optional `reverse` | synonym view |
| `generate_lookup` | `data` | lookup view |
| `generate_spans` | `data`, optional `distance`, `plus_only` | span rules |

The `query` interface defaults to cached JSON finder behavior. `owl` and `data` select direct graph and live finder semantics when an RDF graph is loaded. `ask_json` selects the low-level stored JSON views. Unknown operations and methods return errors. The complete exercised method list is recorded in `tests/fixtures/api/queries.json`.

`load` constructs the new graph and snapshot before replacing the existing ontology. Failed loads retain the previous ontology. A spaCy worker failure also leaves ontology state available for prepared-token operations.

Both worker configuration APIs copy their arguments and start the worker lazily. Configuration accepts a model package or path. The CLI configures this automatically from flags/environment; C library callers configure it explicitly. Executable and script arguments are passed without a shell. Configuration is trusted local process configuration and should not be exposed to unauthenticated remote callers.

The worker deadline defaults to 120 seconds and can be configured up to 3600000 milliseconds. A failed worker request returns an error, disposes of that worker, and permits a fresh startup on the next raw-text request. The failed request is not retried automatically. Engine destruction closes the worker's input and waits briefly for exit, then terminates it if needed. Windows job objects include the virtual environment launcher's child process in cleanup; the native code also waits on the worker interpreter process handle.

Responses are JSON objects. Error code 1 covers I/O/allocation failures, 2 invalid input, 3 RDF syntax, 4 ontology/matching operations, and 5 compatibility worker failures. Invalid JSON from a worker can report code 2. Error responses contain a message and may include line/column information.

The C library's result buffers belong to the caller. Always use `mc_free`, including for errors returned as JSON. A NULL `mc_request` return indicates allocation failure. `mc_destroy` releases engine state and both workers. Calls on distinct engines can use distinct models; access to each individual engine must be serialized.

## Ontology collections

`paths` is an ordered array of file paths. `sources` is an ordered array of ordinary load objects, allowing paths, inline Turtle or prepared snapshots with explicit names. Collection loads are atomic. Arrays merge without duplicate values; canonical ties use source order. A merged RDF graph is available only when every source includes an RDF graph. `interface: "data"` selects live matching and external `<ontology>.owl.txt` synonyms. `class_based` controls extraction independently.

## Arbitrary queries

`result_type` accepts an integer or the corresponding name: `0` / `DO_NOT_TRANSFORM`, `10` / `LIST_OF_STRINGS`, `20` / `DICT_OF_STR2STR`, or `21` / `DICT_OF_STR2LIST`. `22` / `DICT_OF_STR2DICT` returns the same unimplemented-operation error as the reference. Lowercasing defaults to true. Reversal is supported for type 21. The `query` facade also accepts `method: "adhoc"` with positional arguments `[query, result_type, to_lowercase, reverse]` or matching keyword arguments.

Type 0 returns SPARQL Results JSON for SELECT and ASK. Graph queries return `{ "type": "CONSTRUCT", "triples": [...] }` or the corresponding DESCRIBE result. Source null conventions are preserved, including false ASK becoming null. RDF terms contain `kind` (`iri`, `blank` or `literal`), `value`, and optional `datatype`/`language`. Unbound SELECT variables remain absent in raw bindings; transformations that cannot represent them return errors, as in Python.

The query worker does not start spaCy. It caches the current graph and is invalidated on ontology reload. The same backend preserves the reference conversions for date/time, duration, base64 and XML literals during `load`; C callers using those types configure it before loading. `read_rdf` is a graph-syntax operation and does not perform those compatibility conversions.

`transitive` accepts a query method name through `kwargs.query` or its second positional argument. Python callables are represented by that name. See `compatibility.md` for inherited facade defects and work limits.

For arbitrary RDF graphs, `load` accepts `graph_only: true` on a single source. This defers matching-ontology extraction, so SPARQL and direct OWL graph queries can operate on graphs with cyclic blank nodes or subclass relations. Requesting a snapshot or matching operation then materializes the MDA view and applies its work/cycle guards. Ordinary ontology loads materialize it immediately.


The spaCy worker now exposes raw model operations (`analyze` and `retokenize`). Keep the worker script and C library from the same release. An old worker's final LingPatLab token list is not a raw model document and produces an explicit error. `spacy_info.preprocessing` is `mutatoc-c`; `lingpatlab_compatibility` records `1.1.1`, not an installed Python package version.
