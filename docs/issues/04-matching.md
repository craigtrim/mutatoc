Port exact, span and hierarchy matching with the Python implementation as the behavioral reference.

Acceptance:

- [x] Preserve window precedence, blacklist configuration, repeat counts, direction/context/distance rules and canonical selection.
- [x] Preserve nested swap history, token positions, ontology names and confidence.
- [x] Cover optional matching helpers and pre-annotated token input.
- [x] Compare complete results for identical prepared tokens; record ambiguous or defective upstream behavior explicitly.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
