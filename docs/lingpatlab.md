# Native LingPatLab

Mutatoc 0.2.0 implements LingPatLab 1.1.1 in C. The source reference is `craigtrim/lingpatlab` revision `2ed920f1bc7e57d8c74b5b35a54e90f2b7f8ed71`. Its Python syntax trees match the previously installed 1.1.1 distribution.

The runtime no longer installs or imports `lingpatlab`, `wordnet_lookup` or `unicodedata2`. The native library contains the tokenizer dictionaries, source stemmer, 88,013 WordNet membership hashes and the required Unicode tables. The spaCy worker retains the trained model and exposes its attributes and retokenizer. C chooses the input text and merge spans, constructs the output tokens and applies all subsequent rules.

## Calling the API

Use `mc_request` or the persistent CLI with `op: "lingpatlab"`. An ontology does not need to be loaded. Pure text, dictionary, DTO and extraction operations need no Python process. Parsing raw text and sentence segmentation use the configured spaCy model when the source pipeline does.

```json
{"op":"lingpatlab","method":"tokenize_input_text","text":"Dog and cat."}
{"op":"lingpatlab","method":"parse_input_text","text":"Admiral Nimitz arrived."}
{"op":"lingpatlab","method":"stem","text":"caresses"}
{"op":"lingpatlab","method":"is_wordnet_term","text":"phaéton"}
{"op":"lingpatlab","method":"segment_input_text","text":"First paragraph.\n\nSecond paragraph."}
{"op":"lingpatlab","method":"text.title_case","text":"the history of NLP"}
```

`tokenize_input_text` returns the original list of strings, including retained whitespace. `parse_input_text` returns the complete token array, or null when no sentence is produced. `parse_input_tokens` accepts `tokens`; `parse_input_lines` accepts `lines` and returns an array of sentence token arrays. Existing `op: "tokenize"` and ontology parsing retain their previous response shapes.

| API family | Methods and inputs |
| --- | --- |
| Preprocessing | `tokenize_input_text`, `parse_input_text`, `stem`, `is_wordnet_term`, `stopword_exists`, `has_pronoun` take `text`; `pronouns` takes no additional input |
| Dictionaries | `dictionary` takes `name`: `d_hyphens`, `d_currency`, `squotes`, `dquotes`, `d_enclictics`, `d_abbreviations`, `stopwords` or `pronouns` |
| Segmentation | `segment_input_text`, `segment_paragraphs`, `segment_sentences`, `spacy_doc_segmenter`, `bullet_point_cleaner`, `newlines_to_periods`, `numbered_list_normalizer` take `text`; numbered lists accept `denormalize` |
| Delimiters | `delimiters_to_periods` takes `text` and optional `delimiter`, default comma; `post_process_sentences` takes a string array `sentences` |
| Entity patterns | `people_sequence` and `topic_sequence` take a `tokens` array and return `exact`/`fuzzy` lists |
| Entity extraction | `extract_people` and `extract_topics` take `sentences`, an array of token arrays; output is the original name/topic grouping or null |
| Name analysis | `people_analyze` takes `exact` and `fuzzy`; `people_index` takes `unigrams` and `ngrams`; `people_aggregate`, `people_remove_subsumed` and `people_cleanse` take the `people` mapping |
| Phrase filtering | `filter_title_phrases` takes `phrases` |
| Prompt construction | `generate_prompt` takes `text`, optional `version` (1 or 2, default 2), and optional `phrases` for version 1; `generate_sample_prompt` takes optional `version` |
| Model access | `spacy_document` takes `text` and returns raw model `tokens` and `sentences` without LingPatLab preprocessing |

The `text.` prefix selects TextUtils methods. String methods use `text`; `remove_duplicated_phrases` and `jaccard_similarity` also use `text2`. `sliding_window` takes `tokens` and `window_size`. `most_similar_phrase` takes `tokens`, `tokens2`, `window_size` and `score_threshold`. `longest_common_phrase` takes `tokens` and `tokens2`; `find_subsumed_tokens` takes `tokens`. `split_on_len` accepts `threshold`, default 7, and an optional `separator`; the default separator follows the operating system, matching `os.sep`. `split_on_punctuation` accepts the optional `punkt` list. The remaining methods are listed in the [differential manifest](../tests/lingpatlab/parity-manifest.json).

The `dto.` prefix exposes `token_to_string`, `is_noun` and `is_hyphen` with `token`; `sentence_text` and `sentence_to_string` with `tokens`; and `sentences_text` and `sentences_to_string` with `sentences`. `size` accepts either array. JSON-native `to_json`, `to_spacy_result` and `restore_sentences` accept `data`. DTO conversion returns independent JSON values; C callers do not construct Python dataclasses. A `SentencePhrases` value is represented by its sentence token array and phrases array.

## Preserved behavior and adaptations

Version 0.2.2 preserves periods in dotted abbreviations and ellipses. The source tokenizer replaced periods with `~~`, split those markers into separate tokens, and could not restore them. Literal tildes now remain literal. Legacy dictionary values such as `dr~~` are decoded before punctuation splitting. Multi-period words still bypass dictionary expansion.

The original 8,287-case corpus remains unchanged. `tests/lingpatlab/punctuation-corrections.json` identifies 274 corrected expectations, generated with the Python reference tokenizer and parser by `scripts/generate_punctuation_reference.py`. That generator removes the sentinel stages from the Python pipeline and decodes dictionary markers; it never invokes the C implementation. The test verifies the original corpus checksum and the request identity of every correction before comparing complete results.

The source stemmer is preserved as written. For example, `caresses` becomes `care`; substituting a conventional Porter implementation would change compatibility. Tokenizer abbreviation and contraction rules also retain their exact whitespace-sensitive behavior. `has_pronoun` returns a count, despite its Python boolean annotation. WordNet membership retains its lowercase/strip, Unicode compatibility decomposition, ASCII fallback and plural fallback, including the second normalization performed during hash lookup.

The original `Segmenter.input_text` returns nested paragraph/sentence lists but validates them as a flat string list when info logging is enabled. The C API returns the nested result independently of logging. The original empty `parse_input_text` result can trigger a debug-log dereference of `None`; C returns null. Differential fixtures disable source logging to exercise those actual functional results. Empty sentence entries in `parse_input_lines` remain errors.

`longest_common_phrase` uses an unordered Python set when several longest matches tie. The C implementation selects the first common phrase in lexical order. Other collection ordering and the source's extraction/filtering quirks are retained. Unicode operations use the reference versions recorded in the generated-data manifest, rather than the host locale.

The 69-module [implementation map](lingpatlab-map.json) distinguishes native NLP logic, generated data, spaCy model access and language adaptations. Python logging, reflection, decorators, environment/filesystem convenience wrappers, timers and packaging are represented by the existing native error handling, C/OS facilities, JSON interface and build/test tooling. These helpers are not an additional Python runtime dependency or a second public filesystem API.

## Validation and regeneration

The repository retains all 17 original tests and their support files with source hashes. `scripts/run_lingpatlab_upstream.py` substitutes C requests for the Python NLP implementation while retaining the original assertions. Its Python classes are test-side DTO and file-loading adapters.

The independent differential corpus has 8,287 cases across 60 methods, including 204 source error outcomes. It checks complete tokens, Unicode and embedded NUL values, dictionary behavior, segmentation, extraction, DTO formatting and text helpers. Malformed-request regressions check that an error leaves the engine usable. The suite also verifies, in an isolated Python process, that all three retired packages are absent.

```powershell
.\.runtime\Scripts\python.exe tests/test_lingpatlab.py --exe build-msvc/Release/mutatoc.exe --require-clean-runtime
.\.runtime\Scripts\python.exe scripts/run_lingpatlab_upstream.py --exe build-msvc/Release/mutatoc.exe
```

The upstream replay requires the test dependencies in `tests/upstream/requirements.txt`. Corpus regeneration uses a separate reference environment containing LingPatLab 1.1.1, wordnet-lookup 1.3.4, unicodedata2 18.0.0 and the pinned spaCy/model versions. Run `generate_lingpatlab_data.py` and `generate_lingpatlab_parity.py` from that environment, with the source checkout under `.reference/lingpatlab`. Generated data is checked in, so building or running the C port does not require those packages.
