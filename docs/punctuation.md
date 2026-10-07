# Punctuation and exact phrase matching

<!-- Updated for the native tokenizer: craigtrim/mutatoc#1 -->

Mutatoc 0.2.2 matches `U.S. Virgin Islands` as one ontology phrase. Earlier versions replaced periods with temporary tilde markers that could not be restored after tokenization. The same defect affected dictionary entries such as `dr.`, `mr.`, and `mrs.`.

The tokenizer preserves source punctuation and keeps abbreviations as written ([#7](https://github.com/craigtrim/mutatoc/issues/7)). Exact matching derives its maximum phrase length from the loaded ontology. Whitespace-only tokens can occur between phrase words and remain in the returned swap history. Punctuation is never skipped as whitespace. Matches retain longest-phrase priority and leftmost tie selection.

`tests/test_punctuation.c` constructs hypothetical ontologies with `rdfs:label`, `rdfs:seeAlso`, and `skos:altLabel`. Expected matches come from the authored phrases. Original text and arbitrary token metadata are checked independently of canonical output. Source preservation compares every non-whitespace character, because whitespace becomes its own tokens.

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

The suite contains 16,165 cases and 29,981 assertions. It also checks every matched token's retained source history. CTest runs it as `punctuation`:

```powershell
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release -R punctuation --output-on-failure
```

Axiom adds 1,512 complete native parsing cases with independently calculated UTF-16 highlight ranges. Its desktop regression checks full phrase highlighting, Details navigation, repeated spacing, line breaks, and rejection of the wrong year.

## Commas in labels and synonyms

`Equality, Crime, and Justice` matches as one exact entity covering the whole title, with the original commas and offsets. `Crime` alone does not match that title. Commas remain part of each ontology literal; separate values declare separate synonyms. Ontologies using the older packed-list convention can opt into [`comma_lists`](input-formats.md#comma-literals-and-packed-lists).

The `comma_synonyms` suite uses equivalent Turtle, JSON and JSONL fixtures with 24 authored literals across four predicates. Its positive matrix has 19,968 cases spanning six literal shapes, twelve sentence frames, four casings and five comma spacings, loaded from each source format and a reloaded snapshot. The four numeric literals retain their number spacing. Another 5,760 cases reject the isolated comma pieces.

Additional cases cover the reported titles, multi-line lists, Unicode offsets, whitespace, numeric commas mixed with list separators, inline loading in all three source formats, live and deferred loading, collection overrides, failed reloads, and the separate span rules produced by packed `+` lists. Expected entities come from the authored literals and their positions in the input.
