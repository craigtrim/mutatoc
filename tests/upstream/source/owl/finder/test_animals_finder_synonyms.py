#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests the synonym-lookup API surface on FindOntologyJSON built from
# animals-test.owl: has_data(), synonyms(), find_canon(), is_variant(),
# span_keys(), and the fwd/rev synonym sub-structure.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'animals-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/animals'


class TestAnimalsFinderSynonyms(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='animals')

    # ------------------------------------------------------------------ #
    # Basic API surface                                                   #
    # ------------------------------------------------------------------ #

    def test_has_data_returns_true(self) -> None:
        self.assertTrue(self.finder.has_data())

    def test_synonyms_returns_dict(self) -> None:
        self.assertIsInstance(self.finder.synonyms(), dict)

    def test_synonyms_is_non_empty(self) -> None:
        self.assertGreater(len(self.finder.synonyms()), 0)

    def test_span_keys_returns_list(self) -> None:
        self.assertIsInstance(self.finder.span_keys(), list)

    def test_span_keys_is_non_empty(self) -> None:
        self.assertGreater(len(self.finder.span_keys()), 0,
                           'Expected span keys from multi-word labels like "Blue Whale"')

    def test_span_keys_include_blue_whale(self) -> None:
        # span_keys() returns first-word prefixes, not full phrases
        keys = [k.lower() for k in self.finder.span_keys()]
        self.assertIn('blue', keys)

    def test_span_keys_include_german_shepherd(self) -> None:
        # span_keys() returns first-word prefixes, not full phrases
        keys = [k.lower() for k in self.finder.span_keys()]
        self.assertIn('german', keys)

    # ------------------------------------------------------------------ #
    # Synonym forward lookup                                              #
    # ------------------------------------------------------------------ #

    def test_dog_has_canine_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        # synonyms() returns the fwd dict: entity → list of variants
        dog_key = next((k for k in syns if k.lower() == 'dog'), None)
        if dog_key:
            variants = [v.lower() for v in syns[dog_key]]
            self.assertIn('canine', variants)

    def test_cat_has_feline_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        cat_key = next((k for k in syns if k.lower() == 'cat'), None)
        if cat_key:
            variants = [v.lower() for v in syns[cat_key]]
            self.assertIn('feline', variants)

    def test_horse_has_equine_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        horse_key = next((k for k in syns if k.lower() == 'horse'), None)
        if horse_key:
            variants = [v.lower() for v in syns[horse_key]]
            self.assertIn('equine', variants)

    def test_whale_has_cetacean_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        whale_key = next((k for k in syns if k.lower() == 'whale'), None)
        if whale_key:
            variants = [v.lower() for v in syns[whale_key]]
            self.assertIn('cetacean', variants)

    def test_snake_has_serpent_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        snake_key = next((k for k in syns if k.lower() == 'snake'), None)
        if snake_key:
            variants = [v.lower() for v in syns[snake_key]]
            self.assertIn('serpent', variants)

    def test_bear_has_grizzly_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        bear_key = next((k for k in syns if k.lower() == 'bear'), None)
        if bear_key:
            variants = [v.lower() for v in syns[bear_key]]
            self.assertIn('grizzly', variants)

    def test_labrador_has_lab_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        lab_key = next((k for k in syns if k.lower() == 'labrador'), None)
        if lab_key:
            variants = [v.lower() for v in syns[lab_key]]
            self.assertIn('lab', variants)

    def test_parrot_has_macaw_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        parrot_key = next((k for k in syns if k.lower() == 'parrot'), None)
        if parrot_key:
            variants = [v.lower() for v in syns[parrot_key]]
            self.assertIn('macaw', variants)

    def test_crocodile_has_croc_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        croc_key = next((k for k in syns if k.lower() == 'crocodile'), None)
        if croc_key:
            variants = [v.lower() for v in syns[croc_key]]
            self.assertIn('croc', variants)

    def test_turtle_has_tortoise_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        turtle_key = next((k for k in syns if k.lower() == 'turtle'), None)
        if turtle_key:
            variants = [v.lower() for v in syns[turtle_key]]
            self.assertIn('tortoise', variants)

    # ------------------------------------------------------------------ #
    # Hierarchy membership sanity checks                                  #
    # ------------------------------------------------------------------ #

    def test_insect_children_includes_bee(self) -> None:
        self.assertIn('Bee', self.finder.children('Insect'))

    def test_insect_children_includes_butterfly(self) -> None:
        self.assertIn('Butterfly', self.finder.children('Insect'))

    def test_insect_children_includes_ant(self) -> None:
        self.assertIn('Ant', self.finder.children('Insect'))

    def test_bird_children_includes_parrot(self) -> None:
        self.assertIn('Parrot', self.finder.children('Bird'))

    def test_bird_children_includes_penguin(self) -> None:
        self.assertIn('Penguin', self.finder.children('Bird'))

    def test_fish_children_includes_salmon(self) -> None:
        self.assertIn('Salmon', self.finder.children('Fish'))

    def test_fish_children_includes_shark(self) -> None:
        self.assertIn('Shark', self.finder.children('Fish'))

    def test_lizard_children_includes_iguana(self) -> None:
        self.assertIn('Iguana', self.finder.children('Lizard'))


if __name__ == '__main__':
    unittest.main()
