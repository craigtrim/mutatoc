Port the ontology finder and precomputed MDA interfaces so both original OWL inputs and existing JSON snapshots can be used.

Acceptance:

- [x] Preserve forward/reverse synonyms, n-gram lookup, labels, spans, hierarchy, equivalence and predicate views.
- [x] Preserve single/multiple ontology behavior and external synonym files.
- [x] Match class-based and mixed extraction, including existing SKOS/individual fallbacks.
- [x] Compare generated snapshots and query results with the pinned Python reference.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
