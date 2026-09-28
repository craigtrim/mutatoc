# Punctuation and exact phrase matching

Mutatoc 0.2.2 matches `U.S. History to 1865` as one ontology phrase. Earlier versions replaced periods with temporary tilde markers that could not be restored after tokenization. The same defect affected dictionary entries such as `dr.`, `mr.`, and `mrs.`.

The tokenizer now preserves source punctuation and decodes legacy markers only inside abbreviation dictionary values. Exact matching derives its maximum phrase length from the loaded ontology. Whitespace-only tokens can occur between phrase words and remain in the returned swap history. Punctuation is never skipped as whitespace. Matches retain longest-phrase priority and leftmost tie selection.

`tests/test_punctuation.py` constructs hypothetical ontologies with `rdfs:label`, `rdfs:seeAlso`, and `skos:altLabel`. Expected matches come from the authored phrases. Original text and arbitrary token metadata are checked independently of canonical output.

| Cases | Contract |
| ---: | --- |
| 11 | Explicit initialisms, ellipses, decimals, literal tildes, and dictionary abbreviations |
| 5,408 | All 676 two-letter initialisms in eight surrounding contexts |
| 3,000 | Seeded punctuation and Unicode source-preservation cases |
| 2,028 | Prepared-token matching across all initialisms and three annotation properties |
| 2,028 | Wrong-year negative matches |
| 1,944 | Complete raw-text parsing across case, surrounding punctuation, years, and annotation properties |
| 405 | Raw-text near misses and suffix boundaries |
| 13 | Internal whitespace, repeated occurrences, ontology replacement, and snapshot reload |
| 448 | Phrase windows at lengths from 1 to 128, including the old cutoff and integer bit boundaries |
| 840 | Punctuation that must interrupt a phrase |
| 32 | Competing phrases, input order, and repeated longest matches |
| 8 | Malformed lookup rejection and successful recovery in the same engine |

The suite contains 16,165 cases. It also checks every matched token's retained source history. CTest runs it with the existing model integration tests:

```powershell
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
.\.runtime\Scripts\python.exe tests/test_punctuation.py --exe build-msvc/Release/mutatoc.exe --report artifacts/punctuation.json
```

The original LingPatLab differential fixtures are preserved. A separate file identifies 274 corrected outcomes generated from the Python reference tokenizer and parser. The generator never calls Mutatoc. All unaffected reference outcomes remain checked unchanged. See [the reference correction contract](lingpatlab.md).

Axiom adds 1,512 complete native parsing cases with independently calculated UTF-16 highlight ranges. Its desktop regression checks full phrase highlighting, Details navigation, repeated spacing, line breaks, and rejection of the wrong year.
