#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Validates the top-level schema and data types produced by MDAGenerator.generate()

import unittest
from mutato.mda import MDAGenerator

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'

REQUIRED_KEYS = {
    'children', 'parents', 'trie', 'ngrams', 'spans',
    'labels', 'equivalents', 'predicates', 'by_predicate', 'ner', 'synonyms'
}


class TestMDAGeneratorOutputSchema(unittest.TestCase):

    def setUp(self) -> None:
        self.d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()

    def tearDown(self) -> None:
        self.d_owl = None

    def test_all_required_top_level_keys_present(self) -> None:
        for key in REQUIRED_KEYS:
            self.assertIn(key, self.d_owl, f"Missing top-level key: '{key}'")

    def test_synonyms_has_fwd_rev_lookup_subkeys(self) -> None:
        synonyms = self.d_owl['synonyms']
        self.assertIsInstance(synonyms, dict)
        for subkey in ['fwd', 'rev', 'lookup']:
            self.assertIn(subkey, synonyms, f"Missing synonyms subkey: '{subkey}'")

    def test_ngrams_has_levels_one_through_nine(self) -> None:
        ngrams = self.d_owl['ngrams']
        self.assertIsInstance(ngrams, dict)
        for level in range(1, 10):
            self.assertIn(level, ngrams, f"Missing ngram level: {level}")

    def test_ngrams_values_are_lists(self) -> None:
        for level, terms in self.d_owl['ngrams'].items():
            self.assertIsInstance(terms, list, f"ngrams[{level}] is not a list")

    def test_children_is_dict_of_str_to_list(self) -> None:
        children = self.d_owl['children']
        self.assertIsInstance(children, dict)
        for k, v in children.items():
            self.assertIsInstance(k, str, f"children key not str: {k!r}")
            self.assertIsInstance(v, list, f"children[{k!r}] is not a list")

    def test_parents_is_dict_of_str_to_list(self) -> None:
        parents = self.d_owl['parents']
        self.assertIsInstance(parents, dict)
        for k, v in parents.items():
            self.assertIsInstance(k, str)
            self.assertIsInstance(v, list)

    def test_labels_is_non_empty_dict(self) -> None:
        labels = self.d_owl['labels']
        self.assertIsInstance(labels, dict)
        self.assertGreater(len(labels), 0)

    def test_ner_values_are_all_strings(self) -> None:
        ner = self.d_owl['ner']
        self.assertIsInstance(ner, dict)
        for entity, label in ner.items():
            self.assertIsInstance(label, str, f"NER label for '{entity}' is not str")


if __name__ == '__main__':
    unittest.main()
