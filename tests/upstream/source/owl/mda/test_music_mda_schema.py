#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Validates the schema and structure of the MDA dict produced by
# MDAGenerator for the music-test ontology.
# Focuses on: deep hierarchy (4 levels), multi-word spans, altLabel
# synonyms, and structural invariants of all top-level keys.

import unittest
from mutato.mda import MDAGenerator

ONTOLOGY_NAME = 'music-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/music'


class TestMusicMDASchema(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        cls.d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()

    # ------------------------------------------------------------------ #
    # Top-level key presence                                              #
    # ------------------------------------------------------------------ #

    def test_has_all_required_keys(self) -> None:
        required = {'children', 'parents', 'trie', 'ngrams', 'spans',
                    'labels', 'equivalents', 'predicates', 'by_predicate',
                    'ner', 'synonyms'}
        for key in required:
            self.assertIn(key, self.d_owl, f'Missing key: {key}')

    # ------------------------------------------------------------------ #
    # children  - deep hierarchy                                           #
    # ------------------------------------------------------------------ #

    def test_children_music_topic_includes_instrument(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Music_Topic', children)
        self.assertIn('Instrument', children['Music_Topic'])

    def test_children_instrument_includes_string_instrument(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Instrument', children)
        self.assertIn('String_Instrument', children['Instrument'])

    def test_children_string_instrument_includes_guitar(self) -> None:
        children = self.d_owl['children']
        self.assertIn('String_Instrument', children)
        self.assertIn('Guitar', children['String_Instrument'])

    def test_children_guitar_includes_electric_guitar(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Guitar', children)
        self.assertIn('Electric_Guitar', children['Guitar'])

    def test_children_jazz_includes_bebop(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Jazz', children)
        self.assertIn('Bebop', children['Jazz'])

    def test_children_saxophone_includes_alto_saxophone(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Saxophone', children)
        self.assertIn('Alto_Saxophone', children['Saxophone'])

    def test_children_piano_includes_grand_piano(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Piano', children)
        self.assertIn('Grand_Piano', children['Piano'])

    # ------------------------------------------------------------------ #
    # parents  - deep hierarchy                                            #
    # ------------------------------------------------------------------ #

    def test_parents_electric_guitar_includes_guitar(self) -> None:
        parents = self.d_owl['parents']
        self.assertIn('Electric_Guitar', parents)
        self.assertIn('Guitar', parents['Electric_Guitar'])

    def test_parents_guitar_includes_string_instrument(self) -> None:
        parents = self.d_owl['parents']
        self.assertIn('Guitar', parents)
        self.assertIn('String_Instrument', parents['Guitar'])

    def test_parents_bebop_includes_jazz(self) -> None:
        parents = self.d_owl['parents']
        self.assertIn('Bebop', parents)
        self.assertIn('Jazz', parents['Bebop'])

    # ------------------------------------------------------------------ #
    # ngrams  - multi-word label coverage                                  #
    # ------------------------------------------------------------------ #

    def test_ngrams_bigrams_includes_electric_guitar(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'].get(2, [])]
        self.assertIn('electric_guitar', bigrams)

    def test_ngrams_bigrams_includes_smooth_jazz(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'].get(2, [])]
        self.assertIn('smooth_jazz', bigrams)

    def test_ngrams_bigrams_includes_heavy_metal(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'].get(2, [])]
        self.assertIn('heavy_metal', bigrams)

    def test_ngrams_bigrams_includes_classical_music(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'].get(2, [])]
        self.assertIn('classical_music', bigrams)

    def test_ngrams_bigrams_includes_hip_hop(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'].get(2, [])]
        self.assertIn('hip_hop', bigrams)

    # ------------------------------------------------------------------ #
    # spans  - multi-word first-token coverage                             #
    # ------------------------------------------------------------------ #

    def test_spans_contains_electric_token(self) -> None:
        span_keys = [k.lower() for k in self.d_owl['spans']]
        self.assertIn('electric', span_keys)

    def test_spans_contains_smooth_token(self) -> None:
        span_keys = [k.lower() for k in self.d_owl['spans']]
        self.assertIn('smooth', span_keys)

    def test_spans_contains_heavy_token(self) -> None:
        span_keys = [k.lower() for k in self.d_owl['spans']]
        self.assertIn('heavy', span_keys)

    # ------------------------------------------------------------------ #
    # synonyms  - altLabel coverage                                        #
    # ------------------------------------------------------------------ #

    def test_synonyms_fwd_guitar_has_axe(self) -> None:
        fwd = self.d_owl['synonyms']['fwd']
        guitar_key = next((k for k in fwd if k.lower() == 'guitar'), None)
        if guitar_key:
            variants = [v.lower() for v in fwd[guitar_key]]
            self.assertIn('axe', variants)

    def test_synonyms_fwd_saxophone_has_sax(self) -> None:
        fwd = self.d_owl['synonyms']['fwd']
        sax_key = next((k for k in fwd if k.lower() == 'saxophone'), None)
        if sax_key:
            variants = [v.lower() for v in fwd[sax_key]]
            self.assertIn('sax', variants)

    def test_synonyms_fwd_violin_has_fiddle(self) -> None:
        fwd = self.d_owl['synonyms']['fwd']
        vln_key = next((k for k in fwd if k.lower() == 'violin'), None)
        if vln_key:
            variants = [v.lower() for v in fwd[vln_key]]
            self.assertIn('fiddle', variants)

    def test_synonyms_rev_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['synonyms']['rev']), 0)

    # ------------------------------------------------------------------ #
    # ner  - all values constant                                           #
    # ------------------------------------------------------------------ #

    def test_ner_all_values_are_ner_label(self) -> None:
        for v in self.d_owl['ner'].values():
            self.assertEqual(v, 'NER')

    def test_ner_contains_guitar(self) -> None:
        ner_keys = [k.lower() for k in self.d_owl['ner']]
        self.assertIn('guitar', ner_keys)

    # ------------------------------------------------------------------ #
    # labels coverage                                                     #
    # ------------------------------------------------------------------ #

    def test_labels_contains_guitar(self) -> None:
        label_keys = [k.lower() for k in self.d_owl['labels']]
        self.assertIn('guitar', label_keys)

    def test_labels_contains_jazz(self) -> None:
        label_keys = [k.lower() for k in self.d_owl['labels']]
        self.assertIn('jazz', label_keys)

    def test_labels_contains_melody(self) -> None:
        label_keys = [k.lower() for k in self.d_owl['labels']]
        self.assertIn('melody', label_keys)


if __name__ == '__main__':
    unittest.main()
