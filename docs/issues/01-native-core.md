Mutatoc provides a C17 library and Windows executable. Prepared tokens and snapshots work without Python. Full raw-text compatibility retains the original spaCy/LingPatLab frontend; the RDFLib compatibility worker serves arbitrary queries and special literal normalization. The Windows package includes those runtimes.

Acceptance:

- [x] CMake builds a public C API, executable and native tests on Windows and Linux.
- [x] The API defines ownership, errors, UTF-8 input and resource cleanup.
- [x] JSON and Unicode support are built into the distribution.
- [x] Invalid input produces a diagnostic without terminating an embedding application.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
