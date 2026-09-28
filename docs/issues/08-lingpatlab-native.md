Replace the LingPatLab runtime dependency with native C behavior in mutatoc. Keep spaCy as the trained model interface.

The port covers tokenization, retokenization decisions, token construction and normalization, the source stemmer, WordNet membership, sentence/paragraph segmentation, people/topic extraction, DTO operations, dictionaries, text utilities and prompt construction. Python packaging, object wrappers and development helpers use the existing C API and project tooling.

Acceptance:

- [x] Pin and audit all 69 source modules at LingPatLab revision `2ed920f1bc7e57d8c74b5b35a54e90f2b7f8ed71`.
- [x] Retain all 17 upstream tests and fixtures, with hashes and an independent baseline.
- [x] Move linguistic rules into C and reduce the worker to spaCy model calls and attribute serialization.
- [x] Preserve all 393 existing Mutato raw-text/token results.
- [x] Pass expanded differential coverage for LingPatLab APIs, Unicode and source edge cases.
- [x] Run the complete Mutato suite, Windows/Linux native checks and DLL checks.
- [x] Verify a clean runtime and packaged Windows distribution without LingPatLab, wordnet-lookup or unicodedata2.
- [x] Update the compatibility contract, module map, dependency provenance and release artifacts.

Source-specific behavior is the reference, including its stemmer and dictionary normalization. Logging-dependent source defects and language adaptations are recorded explicitly.


Completed locally in `D:\git\mutatos\mutatoc`. All 961 Mutato tests and all 17 LingPatLab tests pass in the runtime without the removed packages. The differential suite passes 8,287 comparisons across 60 methods. Windows MSVC Release and Linux GCC with address/undefined-behavior sanitizers pass all ten CTest groups. The packaged runtime passes the same LingPatLab corpus, all 393 raw-text cases, 315 SPARQL comparisons and the concurrent DLL checks.

The contract is in `docs/lingpatlab.md`, the 69-module accounting is in `docs/lingpatlab-map.json`, and executed results are in `tests/validation.json`. The Windows and complete source archives are under `dist/` as version 0.2.0. Source changes remain local and uncommitted.
