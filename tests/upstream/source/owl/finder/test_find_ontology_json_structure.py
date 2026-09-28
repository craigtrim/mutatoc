#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Verifies that FindOntologyJSON exposes a complete, well-typed API surface

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestFindOntologyJSONStructure(unittest.TestCase):

    def setUp(self) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        self.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='medicopilot')

    def tearDown(self) -> None:
        self.finder = None

    def test_has_data_is_true(self) -> None:
        self.assertTrue(self.finder.has_data())

    def test_synonyms_returns_non_empty_dict(self) -> None:
        result = self.finder.synonyms()
        self.assertIsInstance(result, dict)
        self.assertGreater(len(result), 0)

    def test_synonyms_rev_returns_non_empty_dict(self) -> None:
        result = self.finder.synonyms_rev()
        self.assertIsInstance(result, dict)
        self.assertGreater(len(result), 0)

    def test_lookup_returns_dict(self) -> None:
        result = self.finder.lookup()
        self.assertIsInstance(result, dict)

    def test_ontologies_returns_expected_name(self) -> None:
        self.assertEqual(self.finder.ontologies(), ['medicopilot'])

    def test_trie_returns_dict(self) -> None:
        result = self.finder.trie()
        self.assertIsInstance(result, dict)

    def test_predicates_returns_expected_sorted_list(self) -> None:
        expected = [':inflection', ':locatedIn', ':meansOf', ':requires', ':uses']
        self.assertEqual(sorted(self.finder.predicates()), expected)

    def test_span_keys_are_sorted_by_length_if_present(self) -> None:
        keys = self.finder.span_keys()
        if keys:
            for i in range(len(keys) - 1):
                self.assertLessEqual(
                    len(keys[i]), len(keys[i + 1]),
                    f"span_keys not sorted: '{keys[i]}' > '{keys[i+1]}'"
                )


if __name__ == '__main__':
    unittest.main()
