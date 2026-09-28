Implemented locally in `D:\git\mutatos\mutatoc`:

- The C engine launches a persistent Python worker using the original LingPatLab 1.1.1, spaCy 3.8.2 and en_core_web_sm 3.8.0 pipeline.
- Raw text retains complete statistical annotations and nested token history. The approximate native tokenizer was removed.
- The worker loads once per engine. Missing models, crashes, malformed responses and timeouts return errors. Windows job objects clean up virtual-environment child processes.
- The public C API and CLI accept explicit interpreter, worker and model paths. Runtime setup pins dependencies and verifies the original model archive checksum.
- Large spaCy numeric token hashes retain their exact JSON values.

Validation on Windows: all 961 upstream tests and 4 subtests passed through the retained frontend. All 393 differential cases matched complete preprocessing tokens, matched tokens, nested histories and canonical text. Worker lifecycle and failure tests passed.

The code and setup documentation are in the local working tree. This issue remains open pending publication and review of the repository changes.
