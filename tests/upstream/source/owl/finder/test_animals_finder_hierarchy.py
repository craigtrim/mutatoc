#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests hierarchy traversal (children, parents, ancestors, descendants,
# has_parent, has_ancestor) on FindOntologyJSON built from animals-test.owl.
# The animals ontology has a 4-level tree: Animal → class → species → subspecies.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'animals-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/animals'


class TestAnimalsFinderHierarchy(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='animals')

    # ------------------------------------------------------------------ #
    # children()  - first-level                                            #
    # ------------------------------------------------------------------ #

    def test_animal_children_includes_mammal(self) -> None:
        self.assertIn('Mammal', self.finder.children('Animal'))

    def test_animal_children_includes_bird(self) -> None:
        self.assertIn('Bird', self.finder.children('Animal'))

    def test_animal_children_includes_reptile(self) -> None:
        self.assertIn('Reptile', self.finder.children('Animal'))

    def test_animal_children_includes_fish(self) -> None:
        self.assertIn('Fish', self.finder.children('Animal'))

    def test_animal_children_includes_insect(self) -> None:
        self.assertIn('Insect', self.finder.children('Animal'))

    def test_mammal_children_includes_dog(self) -> None:
        self.assertIn('Dog', self.finder.children('Mammal'))

    def test_mammal_children_includes_cat(self) -> None:
        self.assertIn('Cat', self.finder.children('Mammal'))

    def test_mammal_children_includes_horse(self) -> None:
        self.assertIn('Horse', self.finder.children('Mammal'))

    def test_mammal_children_includes_elephant(self) -> None:
        self.assertIn('Elephant', self.finder.children('Mammal'))

    def test_mammal_children_includes_whale(self) -> None:
        self.assertIn('Whale', self.finder.children('Mammal'))

    def test_mammal_children_includes_lion(self) -> None:
        self.assertIn('Lion', self.finder.children('Mammal'))

    def test_mammal_children_includes_tiger(self) -> None:
        self.assertIn('Tiger', self.finder.children('Mammal'))

    def test_mammal_children_includes_bear(self) -> None:
        self.assertIn('Bear', self.finder.children('Mammal'))

    def test_dog_children_includes_poodle(self) -> None:
        self.assertIn('Poodle', self.finder.children('Dog'))

    def test_dog_children_includes_labrador(self) -> None:
        self.assertIn('Labrador', self.finder.children('Dog'))

    def test_dog_children_includes_german_shepherd(self) -> None:
        self.assertIn('German_Shepherd', self.finder.children('Dog'))

    def test_dog_children_includes_bulldog(self) -> None:
        self.assertIn('Bulldog', self.finder.children('Dog'))

    def test_cat_children_includes_siamese(self) -> None:
        self.assertIn('Siamese', self.finder.children('Cat'))

    def test_cat_children_includes_persian_cat(self) -> None:
        self.assertIn('Persian_Cat', self.finder.children('Cat'))

    def test_cat_children_includes_maine_coon(self) -> None:
        self.assertIn('Maine_Coon', self.finder.children('Cat'))

    def test_whale_children_includes_blue_whale(self) -> None:
        self.assertIn('Blue_Whale', self.finder.children('Whale'))

    def test_whale_children_includes_humpback_whale(self) -> None:
        self.assertIn('Humpback_Whale', self.finder.children('Whale'))

    def test_bear_children_includes_polar_bear(self) -> None:
        self.assertIn('Polar_Bear', self.finder.children('Bear'))

    def test_bear_children_includes_brown_bear(self) -> None:
        self.assertIn('Brown_Bear', self.finder.children('Bear'))

    def test_eagle_children_includes_bald_eagle(self) -> None:
        self.assertIn('Bald_Eagle', self.finder.children('Eagle'))

    def test_eagle_children_includes_golden_eagle(self) -> None:
        self.assertIn('Golden_Eagle', self.finder.children('Eagle'))

    def test_shark_children_includes_great_white_shark(self) -> None:
        self.assertIn('Great_White_Shark', self.finder.children('Shark'))

    def test_shark_children_includes_hammerhead_shark(self) -> None:
        self.assertIn('Hammerhead_Shark', self.finder.children('Shark'))

    def test_snake_children_includes_python(self) -> None:
        self.assertIn('Python', self.finder.children('Snake'))

    def test_snake_children_includes_cobra(self) -> None:
        self.assertIn('Cobra', self.finder.children('Snake'))

    def test_bee_children_includes_honeybee(self) -> None:
        self.assertIn('Honeybee', self.finder.children('Bee'))

    def test_butterfly_children_includes_monarch_butterfly(self) -> None:
        self.assertIn('Monarch_Butterfly', self.finder.children('Butterfly'))

    # ------------------------------------------------------------------ #
    # parents()                                                           #
    # ------------------------------------------------------------------ #

    def test_poodle_parent_is_dog(self) -> None:
        self.assertIn('Dog', self.finder.parents('Poodle'))

    def test_labrador_parent_is_dog(self) -> None:
        self.assertIn('Dog', self.finder.parents('Labrador'))

    def test_german_shepherd_parent_is_dog(self) -> None:
        self.assertIn('Dog', self.finder.parents('German_Shepherd'))

    def test_siamese_parent_is_cat(self) -> None:
        self.assertIn('Cat', self.finder.parents('Siamese'))

    def test_blue_whale_parent_is_whale(self) -> None:
        self.assertIn('Whale', self.finder.parents('Blue_Whale'))

    def test_polar_bear_parent_is_bear(self) -> None:
        self.assertIn('Bear', self.finder.parents('Polar_Bear'))

    def test_bald_eagle_parent_is_eagle(self) -> None:
        self.assertIn('Eagle', self.finder.parents('Bald_Eagle'))

    def test_dog_parent_is_mammal(self) -> None:
        self.assertIn('Mammal', self.finder.parents('Dog'))

    def test_mammal_parent_is_animal(self) -> None:
        self.assertIn('Animal', self.finder.parents('Mammal'))

    # ------------------------------------------------------------------ #
    # ancestors()                                                         #
    # ------------------------------------------------------------------ #

    def test_poodle_ancestors_includes_dog(self) -> None:
        self.assertIn('Dog', self.finder.ancestors('Poodle'))

    def test_poodle_ancestors_includes_mammal(self) -> None:
        self.assertIn('Mammal', self.finder.ancestors('Poodle'))

    def test_poodle_ancestors_includes_animal(self) -> None:
        self.assertIn('Animal', self.finder.ancestors('Poodle'))

    def test_blue_whale_ancestors_includes_whale(self) -> None:
        self.assertIn('Whale', self.finder.ancestors('Blue_Whale'))

    def test_blue_whale_ancestors_includes_mammal(self) -> None:
        self.assertIn('Mammal', self.finder.ancestors('Blue_Whale'))

    def test_blue_whale_ancestors_includes_animal(self) -> None:
        self.assertIn('Animal', self.finder.ancestors('Blue_Whale'))

    def test_bald_eagle_ancestors_includes_eagle(self) -> None:
        self.assertIn('Eagle', self.finder.ancestors('Bald_Eagle'))

    def test_bald_eagle_ancestors_includes_bird(self) -> None:
        self.assertIn('Bird', self.finder.ancestors('Bald_Eagle'))

    # ------------------------------------------------------------------ #
    # has_parent / has_ancestor                                           #
    # ------------------------------------------------------------------ #

    def test_has_parent_poodle_dog(self) -> None:
        self.assertTrue(self.finder.has_parent('Poodle', 'Dog'))

    def test_has_parent_negative(self) -> None:
        self.assertFalse(self.finder.has_parent('Poodle', 'Cat'))

    def test_has_ancestor_poodle_animal(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Poodle', 'Animal'))

    def test_has_ancestor_blue_whale_mammal(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Blue_Whale', 'Mammal'))

    def test_has_ancestor_negative(self) -> None:
        self.assertFalse(self.finder.has_ancestor('Poodle', 'Bird'))

    # ------------------------------------------------------------------ #
    # descendants()                                                       #
    # ------------------------------------------------------------------ #

    def test_dog_descendants_includes_poodle(self) -> None:
        self.assertIn('Poodle', self.finder.descendants('Dog'))

    def test_mammal_descendants_includes_poodle(self) -> None:
        self.assertIn('Poodle', self.finder.descendants('Mammal'))

    def test_animal_descendants_includes_poodle(self) -> None:
        self.assertIn('Poodle', self.finder.descendants('Animal'))

    def test_whale_descendants_includes_blue_whale(self) -> None:
        self.assertIn('Blue_Whale', self.finder.descendants('Whale'))


if __name__ == '__main__':
    unittest.main()
