Provide a native command-line interface and integration boundary for Axiom and other callers.

Acceptance:

- [x] Support ontology loading, parsing, JSON snapshots and structured results.
- [x] Provide a persistent JSON request/response mode that reuses the loaded ontology and spaCy worker.
- [x] Preserve meaningful Python CLI options and document configuration and errors.
- [x] Accept an explicit Python executable path so runtime operation does not depend on Python being on PATH.
- [x] Keep ontology operations and prepared-token matching available without starting Python.
- [x] Document supported operations and measured compatibility limits.

Distribution includes a relocatable Python runtime, pinned dependencies/model, static library, DLL/import library, C header, documentation and checksums. The package is tested without Python on PATH and from a Unicode path.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
