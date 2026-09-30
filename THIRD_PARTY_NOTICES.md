# Third-party components

The C library vendors cJSON 1.7.19 under the MIT license. Its license is in `vendor/cjson/LICENSE`; upstream locations and checksums are in `vendor/manifest.json`. Local patches preserve large integer JSON values and embedded NUL characters, reject malformed string escapes/control characters, and make parser error state thread-local. Embedded NUL uses modified UTF-8 inside the implementation and standard `\u0000` escaping on JSON boundaries; invalid UTF-8 is rejected at input boundaries.

Mutato's ontology fixtures and the request trace recorded from its upstream tests (`tests/fixtures/upstream/trace.json`) are retained under Mutato's MIT license, copied in `LICENSE`. The trace records the reference revision it came from.

The RDF 1.1 Turtle suite under `tests/w3c-turtle` comes from the W3C RDF test repository. Its license notice, source locations, and resource hashes are retained there. `tests/fixtures/w3c-turtle.json` restates the suite's manifest and expected result graphs as JSON; graph comparison treats RDF 1.1 plain literals as `xsd:string` and compares blank nodes by graph isomorphism.

The Unicode tables compiled into the library (`src/unicode_data.h`, `src/uppercase_data.h`, `src/case_data.h` and the numeric ranges in `src/tokenize_data.h`) derive from the Unicode Character Database. Their Unicode, unicodedata2 and PSF license notices and provenance are retained in `vendor/unicode/`. URI resolution follows RFC 3986, with normal and abnormal reference-resolution examples included in the text tests.
