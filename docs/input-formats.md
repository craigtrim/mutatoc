# TTL and JSON ontology inputs

TTL and JSON are equally supported ontology inputs. TTL uses Turtle statements; JSON uses ontology records. Both readers add facts directly to the same C graph and indexes, so both use the same extraction, graph queries, live finder, matching, collections, and optional stages. A collection can contain files in either format.

JSON uses the `mutatoc/1` record schema below. JSONL is another framing of that schema, with one record per line. The schema uses ordinary JSON; it is separate from JSON-LD. Prepared MDA snapshots are also a separate input: snapshots contain extracted matching views, whereas TTL and JSON ontology sources retain the complete RDF graph.

The following TTL and JSON examples contain the same facts. Either one matches `canine` and `hound` to `dog`, with `animal` as its parent.

## TTL

Save this as `animals.ttl`. The repository includes [the complete file](https://github.com/craigtrim/mutatoc/blob/master/examples/animals.ttl).

```turtle
@prefix : <http://example.org/animals#> .
@prefix owl: <http://www.w3.org/2002/07/owl#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix skos: <http://www.w3.org/2004/02/skos/core#> .

:Animal a owl:Class;
    rdfs:label "animal" .

:Dog a owl:Class;
    rdfs:label "dog";
    skos:altLabel "canine", "hound";
    rdfs:subClassOf :Animal;
    owl:backwardCompatibleWith "ANIMAL" .
```

Turtle-encoded `.owl` files use this same reader. Prefixes, relative IRIs, typed and language-tagged literals, blank nodes, RDF lists, and arbitrary predicates retain their existing behavior.

## JSON

Save this as `animals.json`. The repository includes [the complete file](https://github.com/craigtrim/mutatoc/blob/master/examples/animals.json).

```json
[
  {"format":"mutatoc/1", "namespace":"http://example.org/animals#"},
  {"id":"Animal", "type":"owl:Class", "label":"animal"},
  {"id":"Dog", "type":"owl:Class", "label":"dog",
   "synonyms":["canine", "hound"], "parents":["Animal"], "ner":"ANIMAL"}
]
```

## Comma literals and packed lists

Each `label`, `synonyms`, `see_also` or `inflections` value is one literal, including any commas. The corresponding Turtle predicates are `rdfs:label`, `skos:altLabel`, `rdfs:seeAlso` and `inflection`. For example, `"synonyms": ["Reasoning, quantitative"]` matches the complete phrase; it does not declare `Reasoning` and `quantitative` as separate synonyms. `Top 1,000 Words` also stays whole.

Give several synonyms as separate values:

```turtle
:BloodProduct skos:altLabel "PRBC", "PRBCs" .
```

```json
{"id":"BloodProduct","synonyms":["PRBC","PRBCs"]}
```

Older ontologies sometimes pack a list into one literal, such as `"PRBC, PRBCs"`. Load those sources with the boolean option `comma_lists`:

```json
{"op":"load","path":"medical.ttl","comma_lists":true}
```

The CLI accepts the same choice as `--comma-lists`. The option splits each literal at commas, trims the resulting items and drops empty items. A comma immediately between two numeric characters stays inside its item, including Unicode numeric characters, so `Top 1,000 Words` remains a single synonym. A packed span expression such as `stab+injury, penetrate+injury` becomes two span rules. The same option is available on `generate_spans`.

The default is false on each load, including after a load that enabled it. Collection sources inherit the collection's option unless they supply their own boolean value:

```json
{"op":"load","comma_lists":false,"sources":[
  {"path":"courses.json"},
  {"path":"medical.ttl","comma_lists":true}
]}
```

Turtle, JSON and JSONL sources use the same rule, whether loaded from a file or inline, through the live finder or through deferred `graph_only` extraction. This changes the matching views, while graph queries retain the original literals. Prepared snapshots retain the views stored in them; rebuild a snapshot from source to change its comma policy. External `.txt` synonym files retain their separate comma-delimited format.

## Load either format

After building the CLI, load the saved files directly:

```powershell
mutatoc --ontology animals.ttl --input-text "a canine" --json
mutatoc --ontology animals.json --input-text "a canine" --json
```

Both commands return the same matching result, with `a dog` as the canonical text and the original `canine` token in the match history. The file is read directly into the runtime graph; neither input requires reformatting or a converter.

The CLI accepts `--format auto|turtle|ttl|json|jsonl|snapshot`; `auto` is the default. `--format ttl` and `--format json` select the input reader explicitly. `--json` controls output for either reader. Format detection uses content rather than the filename: an array of objects is JSON, an object starts JSONL or a legacy snapshot, and other inputs go to Turtle. Turtle blank-node subjects beginning with `[` remain supported.

## JSONL framing

For `animals.jsonl`, use the same JSON records, one object per line, with no outer brackets or separating commas:

```jsonl
{"format":"mutatoc/1","namespace":"http://example.org/animals#"}
{"id":"Animal","type":"owl:Class","label":"animal"}
{"id":"Dog","type":"owl:Class","label":"dog","synonyms":["canine","hound"],"parents":["Animal"],"ner":"ANIMAL"}
```

Load it with `--ontology animals.jsonl`, optionally adding `--format jsonl`. JSONL has the same record schema and ontology capabilities as the JSON array form.

JSON allows whitespace and line breaks between and within records. JSONL accepts LF or CRLF, an optional final newline, and surrounding spaces on each record's line. Blank lines and multiline records are rejected. Both require UTF-8. JSON comments and trailing commas are rejected.

## Record and term rules

There are three JSON record forms: namespace declarations, entities, and individual triples. They can appear in either JSON or JSONL. Supply class declarations and labels explicitly, just as in TTL. An entity may be declared in several records. References can precede entity declarations, while namespace declarations apply from their position onward.

An entity requires `id`. Values may be a single term or an array of terms. These convenient fields expand to ordinary RDF predicates:

| Field | Predicate | Bare string means |
| --- | --- | --- |
| `type`, `types` | `rdf:type` | resource |
| `label` | `rdfs:label` | literal |
| `synonyms` | `skos:altLabel` | literal |
| `see_also`, `spans` | `rdfs:seeAlso` | literal |
| `parents` | `rdfs:subClassOf` | resource |
| `equivalents` | `owl:equivalentClass` | resource |
| `ner` | `owl:backwardCompatibleWith` | literal |
| `inflections` | `:inflection` | literal |
| `requires` | `:requires` | resource |
| `implies` | `:implies` | resource |
| `similar_to` | `:similarTo` | resource |
| `uses` | `:uses` | resource |
| `effects` | `:effects` | resource |

The `:` predicates require a default namespace. Explicit term objects override the bare-string interpretation. For example, `{"id":"SomeClass"}` is a resource even in a normally literal field. Matching semantics come from these predicates and the selected finder interface, exactly as in TTL. Write existing span expressions in `spans` or `see_also`.

| Term | Example |
| --- | --- |
| Compact resource reference | `{"id":"Dog"}` or `{"id":"ex:Dog"}` |
| Explicit IRI, bypassing prefix expansion | `{"iri":"https://example.org/Dog"}` |
| Blank node | `{"blank":"restriction1"}` |
| Literal | `{"value":"dog"}` |
| Language-tagged literal | `{"value":"chien","language":"fr"}` |
| Typed literal | `{"value":"001","datatype":"xsd:integer"}` |

The existing `triples` term form also works: `{"value":"https://example.org/Dog","kind":"iri"}`, with `kind` equal to `iri`, `blank`, or `literal`, and optional `datatype` or `language` for literals. A term has exactly one of `id`, `iri`, `blank`, or `value`. A subject must be a resource and a predicate must be an IRI.

Literal lexical values are strings. Write numbers and booleans as quoted lexical values with an explicit datatype to avoid JSON number rounding or implicit type inference. Literal normalization is shared with Turtle: common XSD types retain the existing Mutato normalization behavior, while other lexical forms remain intact. Escape decoding preserves Unicode and escaped NUL in literals. Language tags and their datatype must agree.

## Namespaces and relative IRIs

```json
{"base":"https://example.org/ontologies/",
 "namespace":"animals#",
 "prefixes":{"ex":"https://example.org/extra#"}}
```

Metadata records accept `format` (optional, currently `mutatoc/1`), `base`, `namespace`, and `prefixes`. `namespace` is shorthand for the empty prefix in `prefixes`; declare it only once per record. Structured resource references recognize the built-in graph prefixes, including `rdf`, `rdfs`, `owl`, `skos`, and `xsd`.

An unqualified reference such as `Dog` uses the default namespace when one is declared. Known prefixes expand compact names. Other references resolve against the current base; explicit `iri` terms always use IRI resolution without prefix substitution. File loads begin with the file URI as their base; inline loads can supply `base` in the request. A metadata record resolves its base first and its namespace declarations second. Prefix rebinding follows the same graph/query binding policy as Turtle.

## Arbitrary predicates, restrictions, and lists

The short fields are conveniences. `properties` accepts any RDF predicate, so the schema imposes no fixed vocabulary:

```json
{"id":"Dog","properties":{
  "ex:status":"active",
  "ex:related":{"id":"Cat"},
  "ex:count":{"value":"9007199254740993","datatype":"xsd:integer"}
}}
```

Strings in `properties` are literals; wrap resource references in `id`, `iri`, or `blank`. For ordered repeated predicates, use `facts`:

```json
{"id":"Dog","facts":[
  {"predicate":"rdfs:label","value":"dog"},
  {"predicate":"ex:related","value":{"id":"Cat"}},
  {"predicate":"rdfs:label","value":{"value":"chien","language":"fr"}}
]}
```

An individual triple is another record form. This example expresses an OWL restriction:

```json
[
  {"namespace":"https://example.org/animals#"},
  {"id":"Dog","parents":{"blank":"r1"}},
  {"subject":{"blank":"r1"},"predicate":"rdf:type","object":{"id":"owl:Restriction"}},
  {"subject":{"blank":"r1"},"predicate":"owl:onProperty","object":{"id":"hasPart"}},
  {"subject":{"blank":"r1"},"predicate":"owl:someValuesFrom","object":{"id":"Tail"}}
]
```

RDF lists use the same blank nodes with `rdf:first`, `rdf:rest`, and `rdf:nil`. Named individuals, custom annotations, typed literals, restrictions, and any other triples accepted by the Turtle reader can be expressed this way. These facts receive the engine's existing query and extraction behavior with either input format.

Record, array, and fact order are retained, and the shared graph builder removes duplicate triples. Object fields are processed in their supplied order; use `facts` when order must survive tools that reorder JSON object keys. Blank-node identifiers are scoped per source document, including mixed-format collections.

## C and JSON protocol

The public C API and ownership rules are unchanged. Use `mc_request` with a file path:

```json
{"op":"load","path":"animals.json","format":"json","interface":"data"}
```

For inline source, `content` is the UTF-8 source text encoded as a request string:

```json
{"op":"load","format":"json","content":"[{\"id\":\"https://example.org/Dog\",\"label\":\"dog\"}]"}
```

`content` and `format` also work with `read_rdf` and `detect_schema`. Existing inline `turtle` and prepared `snapshot` requests continue to work. `class_based`, `distance`, `interface`, `base`, and `graph_only` retain their existing meanings. A JSON source provides a graph for `triples`, `owl`, and `data` queries; a prepared MDA snapshot does not.

Mixed-format collections load directly:

```json
{"op":"load","sources":[
  {"path":"animals.ttl","name":"animals"},
  {"path":"medical.json","name":"medical"},
  {"path":"courses.jsonl","name":"courses"}
]}
```

Source order still resolves canonical ties. Live external synonyms use the source filename plus `.txt`, for example `animals.jsonl.txt`, under the existing `interface: "data"` behavior. Failed single-source or collection loads leave the previous ontology available. Unknown fields, duplicate keys, invalid terms, and malformed records return errors with source locations rather than dropping facts.

## Implementation and verification

`source.c` selects the reader. `rdf.c` reads Turtle and `json_rdf.c` reads JSON/JSONL. `graph.c` owns the common triple store, deduplication, prefix bindings, and indexes. Resource resolution, field-predicate mappings, and fact emission are shared helpers in `source.h`. Format readers supply graph facts; extraction and matching operate on the graph independently of the source syntax.

The current file API retains a raw source buffer, bounded at 256 MiB. JSON parsing builds a temporary tree for one record and releases it after adding its facts. There is no whole-document JSON tree, intermediate serialization, temporary conversion file, or converter process in the loading path. Graphs and the matching views required by the existing engine remain resident.

The `sources` suite runs 5,804 checks. It round-trips all 19 ontology fixtures and 145 W3C evaluation graphs through JSON and JSONL, compares all 1,659 finder requests and 393 raw/prepared matching cases for each format against TTL, and checks optional stages, mixed collections, failure atomicity, and malformed inputs. These comparisons cover graph terms, literal attributes, triple order, prefixes, snapshots, and complete public responses. Counts include repeated comparisons across formats; they are not counts of distinct ontologies.

For repeatable load benchmarks, the test harness can write equivalent source fixtures in a separate process. This conversion is test preparation only:

```powershell
New-Item -ItemType Directory -Force artifacts/source-bench
build-msvc/Release/mutatoc_sources.exe . --write-sources artifacts/source-bench
build-msvc/Release/mutatoc_bench.exe . --runs 7
build-msvc/Release/mutatoc_bench.exe . --runs 7 --source-dir artifacts/source-bench --extension .json
build-msvc/Release/mutatoc_bench.exe . --runs 7 --source-dir artifacts/source-bench --extension .jsonl
```

These generated fixtures use explicit terms and ordered facts to preserve all graph details. They measure correctness-preserving load paths rather than the minimum possible JSON size. Token counts depend on vocabulary, record shape, and tokenizer; JSONL framing alone does not guarantee fewer LLM tokens than a compact JSON array.
