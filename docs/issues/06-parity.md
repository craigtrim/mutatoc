Establish traceable coverage for all 961 upstream test methods and keep unchanged ontology fixtures as compatibility inputs.

Acceptance:

- [x] Record the source revision, dependency/model versions and fixture hashes.
- [x] Run the original Python suite and record baseline failures separately from port regressions.
- [x] Map every test to a native equivalent or an explicitly documented unresolved case.
- [x] Compare OWL extraction, JSON queries, prepared-token matching and raw-text results independently.
- [x] Native and worker tests pass locally on Windows and Linux; the CI workflow runs the same compatibility corpus.
- [x] No completion claim while semantic differences remain unexplained.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
