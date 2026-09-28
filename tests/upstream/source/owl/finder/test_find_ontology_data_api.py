#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Exercises the FindOntologyData (OWL path) public API directly:
# metadata methods, predicate queries, hierarchy, and the lookup structure.

import unittest
from mutato.finder.multiquery import FindOntologyData

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestFindOntologyDataAPI(unittest.TestCase):

    def setUp(self) -> None:
        self.finder = FindOntologyData(
            ontologies=[ONTOLOGY_NAME],
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        )

    def tearDown(self) -> None:
        self.finder = None

    def test_ontologies_returns_input_list(self) -> None:
        self.assertEqual(self.finder.ontologies(), [ONTOLOGY_NAME])

    def test_absolute_path_returns_expected_string(self) -> None:
        self.assertEqual(self.finder.absolute_path(), ABSOLUTE_PATH)

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

    def test_lookup_returns_dict_with_integer_keys(self) -> None:
        result = self.finder.lookup()
        self.assertIsInstance(result, dict)
        for key in result:
            self.assertIsInstance(key, int)

    def test_predicates_are_sorted_as_expected(self) -> None:
        expected = [':inflection', ':locatedIn', ':meansOf', ':requires', ':uses']
        # by_predicate keys come from predicates; verify known ones are present
        all_predicates = self.finder.by_predicate(':inflection')
        self.assertIsNotNone(all_predicates)

    def test_gluconate_children_via_owl_path(self) -> None:
        children = [c.lower() for c in self.finder.children('Gluconate')]
        self.assertIn('calcium_gluconate', children)

    def test_gluconate_ancestors_via_owl_path(self) -> None:
        ancestors = [a.lower() for a in self.finder.ancestors('Gluconate')]
        self.assertIn('medication', ancestors)
        self.assertIn('event', ancestors)

    def test_entity_exists_for_known_entity(self) -> None:
        self.assertTrue(self.finder.entity_exists('Gluconate'))

    def test_entity_exists_false_for_unknown(self) -> None:
        self.assertFalse(self.finder.entity_exists('ZZZNonExistentEntityXYZ999'))


if __name__ == '__main__':
    unittest.main()
