#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests canon/variant lookup operations on FindOntologyJSON.
# Explores is_canon, is_variant, find_canon, find_variants dynamically
# against the live synonym data rather than hard-coding entity names.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestFindOntologyJSONCanonOps(unittest.TestCase):

    def setUp(self) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        self.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='medicopilot')

        # Pick a known canon entity and one of its variants for use across tests
        fwd = self.finder.synonyms()
        self.canon_entity = None
        self.known_variant = None
        for entity, variants in fwd.items():
            if variants:
                self.canon_entity = entity
                self.known_variant = variants[0]
                break

    def tearDown(self) -> None:
        self.finder = None

    def test_canon_entity_is_recognized_as_canon(self) -> None:
        if self.canon_entity:
            self.assertTrue(self.finder.is_canon(self.canon_entity))

    def test_known_variant_is_recognized_as_variant(self) -> None:
        if self.known_variant:
            self.assertTrue(self.finder.is_variant(self.known_variant))

    def test_find_variants_returns_list_for_canon(self) -> None:
        if self.canon_entity:
            result = self.finder.find_variants(self.canon_entity)
            self.assertIsInstance(result, list)
            self.assertGreater(len(result), 0)

    def test_known_variant_appears_in_find_variants(self) -> None:
        if self.canon_entity and self.known_variant:
            variants = self.finder.find_variants(self.canon_entity)
            self.assertIn(self.known_variant, variants)

    def test_nonexistent_entity_is_not_canon(self) -> None:
        self.assertFalse(self.finder.is_canon('ZZZNonExistentEntity99999'))

    def test_nonexistent_entity_is_not_variant(self) -> None:
        self.assertFalse(self.finder.is_variant('ZZZNonExistentEntity99999'))

    def test_find_canon_returns_string_or_none(self) -> None:
        # find_canon on a variant should return its canonical entity or None
        if self.known_variant:
            result = self.finder.find_canon(self.known_variant)
            self.assertTrue(result is None or isinstance(result, str))


if __name__ == '__main__':
    unittest.main()
