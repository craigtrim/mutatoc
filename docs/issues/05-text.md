Retain the original spaCy and LingPatLab preprocessing behind a persistent worker called by the C engine. The C port must preserve statistical annotations and token history as well as ontology matches.

The agreed implementation uses Python for preprocessing. Replacing LingPatLab rules remains a possible later change that requires separate parity evidence.

Acceptance:

- [x] Load spaCy and its trained model once per engine, with explicit executable, worker and model configuration.
- [x] Preserve the complete LingPatLab token payload, including POS, dependencies, entity labels, coordinates and numeric identifiers.
- [x] Compare complete raw-text results and nested swap histories with the pinned Python reference.
- [x] Report startup failures, missing models, malformed worker responses, crashes and timeouts explicitly. Never fall back to approximate tokenization.
- [x] Clean up the worker and its child processes on reconfiguration and engine destruction.
- [x] Record runtime dependency versions and model checksum, and provide setup instructions.

Implementation and validation are complete in the local `D:\git\mutatos\mutatoc` workspace. Source files are currently uncommitted. The recorded contract and source-defect decisions are in `docs/compatibility.md`; executed validation is recorded in `tests/validation.json`.

## Executed validation

All 961 upstream tests pass, along with 393 complete raw-text cases, 1,665 public API calls, 315 SPARQL calls and 313 W3C Turtle tests. The 19 original OWL graphs contain 223,091 triples and are isomorphic to the reference. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers each pass all nine CTest groups. The shared-library ABI also passes concurrent-engine and real preprocessing checks.
