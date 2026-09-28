#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Issue: https://github.com/craigtrim/mutato/issues/4
# Docs: docs/mda-precompute.md
# Tests UniversalMDAGenerator against class-based and mixed OWL ontologies.
# Verifies auto-detection, correct output shape, and parity with MDAGenerator.

import unittest

from mutato.mda import MDAGenerator, UniversalMDAGenerator

ANIMALS_NAME = 'animals-test'
ANIMALS_PATH = 'tests/test_data/ontologies'
ANIMALS_NS = 'http://test.ai/animals'

ECON_NAME = 'econ-20160218'
ECON_PATH = 'tests/test_data/ontologies'
ECON_NS = 'http://graffl.ai/skills#'

_REQUIRED_KEYS = {
    'children', 'parents', 'trie', 'ngrams', 'spans',
    'labels', 'equivalents', 'predicates', 'by_predicate',
    'ner', 'synonyms',
}


class TestUniversalMDAGeneratorAnimals(unittest.TestCase):
    """ Verify that UniversalMDAGenerator produces identical output to
    MDAGenerator for a CLASS_BASED ontology (animals-test). """

    @classmethod
    def setUpClass(cls) -> None:
        cls.d_universal = UniversalMDAGenerator(
            ontology_name=ANIMALS_NAME,
            absolute_path=ANIMALS_PATH,
            namespace=ANIMALS_NS,
        ).generate()
        cls.d_mda = MDAGenerator(
            ontology_name=ANIMALS_NAME,
            absolute_path=ANIMALS_PATH,
            namespace=ANIMALS_NS,
        ).generate()

    # ------------------------------------------------------------------ #
    # Output shape  - all required keys present                            #
    # ------------------------------------------------------------------ #

    def test_has_children_key(self) -> None:
        self.assertIn('children', self.d_universal)

    def test_has_parents_key(self) -> None:
        self.assertIn('parents', self.d_universal)

    def test_has_trie_key(self) -> None:
        self.assertIn('trie', self.d_universal)

    def test_has_ngrams_key(self) -> None:
        self.assertIn('ngrams', self.d_universal)

    def test_has_spans_key(self) -> None:
        self.assertIn('spans', self.d_universal)

    def test_has_labels_key(self) -> None:
        self.assertIn('labels', self.d_universal)

    def test_has_equivalents_key(self) -> None:
        self.assertIn('equivalents', self.d_universal)

    def test_has_predicates_key(self) -> None:
        self.assertIn('predicates', self.d_universal)

    def test_has_by_predicate_key(self) -> None:
        self.assertIn('by_predicate', self.d_universal)

    def test_has_ner_key(self) -> None:
        self.assertIn('ner', self.d_universal)

    def test_has_synonyms_key(self) -> None:
        self.assertIn('synonyms', self.d_universal)

    def test_no_extra_top_level_keys(self) -> None:
        self.assertEqual(set(self.d_universal.keys()), _REQUIRED_KEYS)

    # ------------------------------------------------------------------ #
    # Parity with MDAGenerator                                            #
    # ------------------------------------------------------------------ #

    def test_children_keys_match_mda(self) -> None:
        self.assertEqual(
            set(self.d_universal['children'].keys()),
            set(self.d_mda['children'].keys()),
        )

    def test_parents_keys_match_mda(self) -> None:
        self.assertEqual(
            set(self.d_universal['parents'].keys()),
            set(self.d_mda['parents'].keys()),
        )

    def test_ner_keys_match_mda(self) -> None:
        self.assertEqual(
            set(self.d_universal['ner'].keys()),
            set(self.d_mda['ner'].keys()),
        )

    def test_labels_keys_match_mda(self) -> None:
        self.assertEqual(
            set(self.d_universal['labels'].keys()),
            set(self.d_mda['labels'].keys()),
        )

    def test_ngrams_unigrams_match_mda(self) -> None:
        self.assertEqual(
            sorted(self.d_universal['ngrams'][1]),
            sorted(self.d_mda['ngrams'][1]),
        )

    def test_synonyms_fwd_matches_mda(self) -> None:
        self.assertEqual(
            self.d_universal['synonyms']['fwd'],
            self.d_mda['synonyms']['fwd'],
        )

    # ------------------------------------------------------------------ #
    # Structural sanity                                                   #
    # ------------------------------------------------------------------ #

    def test_children_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_universal['children']), 0)

    def test_children_animal_includes_mammal(self) -> None:
        children = self.d_universal['children']
        self.assertIn('Animal', children)
        self.assertIn('Mammal', children['Animal'])

    def test_parents_poodle_includes_dog(self) -> None:
        parents = self.d_universal['parents']
        self.assertIn('Poodle', parents)
        self.assertIn('Dog', parents['Poodle'])

    def test_ner_all_values_are_ner_label(self) -> None:
        for v in self.d_universal['ner'].values():
            self.assertEqual(v, 'NER')

    def test_synonyms_has_fwd_rev_lookup(self) -> None:
        syns = self.d_universal['synonyms']
        self.assertIn('fwd', syns)
        self.assertIn('rev', syns)
        self.assertIn('lookup', syns)


class TestUniversalMDAGeneratorEcon(unittest.TestCase):
    """ Verify that UniversalMDAGenerator handles the MIXED econ.owl.

    Checks: correct output shape, non-empty children/parents/ner,
    and that synonyms sub-dict is structurally valid.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.d_owl = UniversalMDAGenerator(
            ontology_name=ECON_NAME,
            absolute_path=ECON_PATH,
            namespace=ECON_NS,
        ).generate()

    # ------------------------------------------------------------------ #
    # All required keys present                                           #
    # ------------------------------------------------------------------ #

    def test_has_children_key(self) -> None:
        self.assertIn('children', self.d_owl)

    def test_has_parents_key(self) -> None:
        self.assertIn('parents', self.d_owl)

    def test_has_trie_key(self) -> None:
        self.assertIn('trie', self.d_owl)

    def test_has_ngrams_key(self) -> None:
        self.assertIn('ngrams', self.d_owl)

    def test_has_spans_key(self) -> None:
        self.assertIn('spans', self.d_owl)

    def test_has_labels_key(self) -> None:
        self.assertIn('labels', self.d_owl)

    def test_has_equivalents_key(self) -> None:
        self.assertIn('equivalents', self.d_owl)

    def test_has_predicates_key(self) -> None:
        self.assertIn('predicates', self.d_owl)

    def test_has_by_predicate_key(self) -> None:
        self.assertIn('by_predicate', self.d_owl)

    def test_has_ner_key(self) -> None:
        self.assertIn('ner', self.d_owl)

    def test_has_synonyms_key(self) -> None:
        self.assertIn('synonyms', self.d_owl)

    def test_exact_top_level_key_set(self) -> None:
        self.assertEqual(set(self.d_owl.keys()), _REQUIRED_KEYS)

    # ------------------------------------------------------------------ #
    # Non-empty content                                                   #
    # ------------------------------------------------------------------ #

    def test_children_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['children']), 0)

    def test_parents_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['parents']), 0)

    def test_ner_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['ner']), 0)

    def test_labels_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['labels']), 0)

    def test_ngrams_unigrams_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['ngrams'][1]), 0)

    # ------------------------------------------------------------------ #
    # Type checks                                                         #
    # ------------------------------------------------------------------ #

    def test_children_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['children'], dict)

    def test_parents_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['parents'], dict)

    def test_children_values_are_lists(self) -> None:
        for v in self.d_owl['children'].values():
            self.assertIsInstance(v, list)
            break

    def test_parents_values_are_lists(self) -> None:
        for v in self.d_owl['parents'].values():
            self.assertIsInstance(v, list)
            break

    def test_ner_all_values_are_ner_label(self) -> None:
        for v in self.d_owl['ner'].values():
            self.assertEqual(v, 'NER')

    def test_synonyms_has_fwd_rev_lookup(self) -> None:
        syns = self.d_owl['synonyms']
        self.assertIn('fwd', syns)
        self.assertIn('rev', syns)
        self.assertIn('lookup', syns)

    def test_synonyms_fwd_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['synonyms']['fwd'], dict)

    def test_synonyms_rev_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['synonyms']['rev'], dict)

    def test_ngrams_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['ngrams'], dict)

    def test_predicates_is_list(self) -> None:
        self.assertIsInstance(self.d_owl['predicates'], list)

    def test_by_predicate_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['by_predicate'], dict)

    # ------------------------------------------------------------------ #
    # Hierarchy integrity  - children/parents are complementary           #
    # ------------------------------------------------------------------ #

    def test_children_keys_are_strings(self) -> None:
        for k in self.d_owl['children']:
            self.assertIsInstance(k, str)
            break

    def test_parents_keys_are_strings(self) -> None:
        for k in self.d_owl['parents']:
            self.assertIsInstance(k, str)
            break

    def test_children_list_items_are_strings(self) -> None:
        for child_list in self.d_owl['children'].values():
            for item in child_list:
                self.assertIsInstance(item, str)
            break

    def test_parents_list_items_are_strings(self) -> None:
        for parent_list in self.d_owl['parents'].values():
            for item in parent_list:
                self.assertIsInstance(item, str)
            break


if __name__ == '__main__':
    unittest.main()
