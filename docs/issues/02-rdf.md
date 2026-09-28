The same Turtle-encoded OWL files accepted by the Python reference must load unchanged in C.

Acceptance:

- [x] Implement RDF terms, indexed triples and Turtle syntax, including relative IRIs, prefixes, blank nodes, collections, typed/language literals and escapes.
- [x] Preserve Mutato schema detection, extraction and traversal behavior across all 19 upstream OWL fixtures.
- [x] Exercise cycles, malformed files, Unicode and blank-node extraction.
- [x] Account explicitly for the public ad hoc SPARQL API as well as fixed internal queries.

The RDF parser and fixed ontology queries are native. Arbitrary SPARQL uses pinned RDFLib through an isolated worker; result transformations are native. The same worker preserves date/duration/base64/XML literal conversions. This preserves the full exposed query contract.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
