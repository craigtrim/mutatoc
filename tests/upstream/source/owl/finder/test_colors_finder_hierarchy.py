#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests hierarchy traversal on FindOntologyJSON built from colors-test.owl.
# Color → {Primary_Color, Secondary_Color, Neutral_Color, Warm_Color,
#           Cool_Color, Metallic_Color} → individual colors.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'colors-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/colors'


class TestColorsFinderHierarchy(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='colors')

    # ------------------------------------------------------------------ #
    # children()  - Color root                                             #
    # ------------------------------------------------------------------ #

    def test_color_children_includes_primary(self) -> None:
        self.assertIn('Primary_Color', self.finder.children('Color'))

    def test_color_children_includes_secondary(self) -> None:
        self.assertIn('Secondary_Color', self.finder.children('Color'))

    def test_color_children_includes_neutral(self) -> None:
        self.assertIn('Neutral_Color', self.finder.children('Color'))

    def test_color_children_includes_warm(self) -> None:
        self.assertIn('Warm_Color', self.finder.children('Color'))

    def test_color_children_includes_cool(self) -> None:
        self.assertIn('Cool_Color', self.finder.children('Color'))

    def test_color_children_includes_metallic(self) -> None:
        self.assertIn('Metallic_Color', self.finder.children('Color'))

    # ------------------------------------------------------------------ #
    # children()  - Primary colors                                         #
    # ------------------------------------------------------------------ #

    def test_primary_color_children_includes_red(self) -> None:
        self.assertIn('Red', self.finder.children('Primary_Color'))

    def test_primary_color_children_includes_blue(self) -> None:
        self.assertIn('Blue', self.finder.children('Primary_Color'))

    def test_primary_color_children_includes_yellow(self) -> None:
        self.assertIn('Yellow', self.finder.children('Primary_Color'))

    # ------------------------------------------------------------------ #
    # children()  - Secondary colors                                       #
    # ------------------------------------------------------------------ #

    def test_secondary_color_children_includes_orange(self) -> None:
        self.assertIn('Orange', self.finder.children('Secondary_Color'))

    def test_secondary_color_children_includes_green(self) -> None:
        self.assertIn('Green', self.finder.children('Secondary_Color'))

    def test_secondary_color_children_includes_purple(self) -> None:
        self.assertIn('Purple', self.finder.children('Secondary_Color'))

    def test_secondary_color_children_includes_indigo(self) -> None:
        self.assertIn('Indigo', self.finder.children('Secondary_Color'))

    # ------------------------------------------------------------------ #
    # children()  - Neutral colors                                         #
    # ------------------------------------------------------------------ #

    def test_neutral_color_children_includes_black(self) -> None:
        self.assertIn('Black', self.finder.children('Neutral_Color'))

    def test_neutral_color_children_includes_white(self) -> None:
        self.assertIn('White', self.finder.children('Neutral_Color'))

    def test_neutral_color_children_includes_gray(self) -> None:
        self.assertIn('Gray', self.finder.children('Neutral_Color'))

    def test_neutral_color_children_includes_brown(self) -> None:
        self.assertIn('Brown', self.finder.children('Neutral_Color'))

    # ------------------------------------------------------------------ #
    # children()  - Warm / Cool / Metallic                                 #
    # ------------------------------------------------------------------ #

    def test_warm_color_children_includes_pink(self) -> None:
        self.assertIn('Pink', self.finder.children('Warm_Color'))

    def test_warm_color_children_includes_coral(self) -> None:
        self.assertIn('Coral', self.finder.children('Warm_Color'))

    def test_warm_color_children_includes_maroon(self) -> None:
        self.assertIn('Maroon', self.finder.children('Warm_Color'))

    def test_cool_color_children_includes_teal(self) -> None:
        self.assertIn('Teal', self.finder.children('Cool_Color'))

    def test_cool_color_children_includes_turquoise(self) -> None:
        self.assertIn('Turquoise', self.finder.children('Cool_Color'))

    def test_cool_color_children_includes_navy(self) -> None:
        self.assertIn('Navy', self.finder.children('Cool_Color'))

    def test_cool_color_children_includes_mint(self) -> None:
        self.assertIn('Mint', self.finder.children('Cool_Color'))

    def test_metallic_color_children_includes_silver(self) -> None:
        self.assertIn('Silver', self.finder.children('Metallic_Color'))

    def test_metallic_color_children_includes_bronze(self) -> None:
        self.assertIn('Bronze', self.finder.children('Metallic_Color'))

    def test_metallic_color_children_includes_gold_metallic(self) -> None:
        self.assertIn('Gold_Metallic', self.finder.children('Metallic_Color'))

    # ------------------------------------------------------------------ #
    # parents()                                                           #
    # ------------------------------------------------------------------ #

    def test_red_parent_is_primary_color(self) -> None:
        self.assertIn('Primary_Color', self.finder.parents('Red'))

    def test_blue_parent_is_primary_color(self) -> None:
        self.assertIn('Primary_Color', self.finder.parents('Blue'))

    def test_yellow_parent_is_primary_color(self) -> None:
        self.assertIn('Primary_Color', self.finder.parents('Yellow'))

    def test_green_parent_is_secondary_color(self) -> None:
        self.assertIn('Secondary_Color', self.finder.parents('Green'))

    def test_purple_parent_is_secondary_color(self) -> None:
        self.assertIn('Secondary_Color', self.finder.parents('Purple'))

    def test_gray_parent_is_neutral_color(self) -> None:
        self.assertIn('Neutral_Color', self.finder.parents('Gray'))

    def test_pink_parent_is_warm_color(self) -> None:
        self.assertIn('Warm_Color', self.finder.parents('Pink'))

    def test_teal_parent_is_cool_color(self) -> None:
        self.assertIn('Cool_Color', self.finder.parents('Teal'))

    def test_silver_parent_is_metallic_color(self) -> None:
        self.assertIn('Metallic_Color', self.finder.parents('Silver'))

    def test_primary_color_parent_is_color(self) -> None:
        self.assertIn('Color', self.finder.parents('Primary_Color'))

    def test_secondary_color_parent_is_color(self) -> None:
        self.assertIn('Color', self.finder.parents('Secondary_Color'))

    # ------------------------------------------------------------------ #
    # ancestors()                                                         #
    # ------------------------------------------------------------------ #

    def test_red_ancestors_includes_primary_color(self) -> None:
        self.assertIn('Primary_Color', self.finder.ancestors('Red'))

    def test_red_ancestors_includes_color(self) -> None:
        self.assertIn('Color', self.finder.ancestors('Red'))

    def test_green_ancestors_includes_secondary_color(self) -> None:
        self.assertIn('Secondary_Color', self.finder.ancestors('Green'))

    def test_green_ancestors_includes_color(self) -> None:
        self.assertIn('Color', self.finder.ancestors('Green'))

    def test_teal_ancestors_includes_cool_color(self) -> None:
        self.assertIn('Cool_Color', self.finder.ancestors('Teal'))

    def test_teal_ancestors_includes_color(self) -> None:
        self.assertIn('Color', self.finder.ancestors('Teal'))

    def test_silver_ancestors_includes_metallic_color(self) -> None:
        self.assertIn('Metallic_Color', self.finder.ancestors('Silver'))

    def test_silver_ancestors_includes_color(self) -> None:
        self.assertIn('Color', self.finder.ancestors('Silver'))

    # ------------------------------------------------------------------ #
    # has_parent / has_ancestor                                           #
    # ------------------------------------------------------------------ #

    def test_has_parent_red_primary_color(self) -> None:
        self.assertTrue(self.finder.has_parent('Red', 'Primary_Color'))

    def test_has_parent_negative_red_neutral(self) -> None:
        self.assertFalse(self.finder.has_parent('Red', 'Neutral_Color'))

    def test_has_ancestor_red_color(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Red', 'Color'))

    def test_has_ancestor_teal_color(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Teal', 'Color'))

    def test_has_ancestor_negative_red_cool(self) -> None:
        self.assertFalse(self.finder.has_ancestor('Red', 'Cool_Color'))

    # ------------------------------------------------------------------ #
    # descendants()                                                       #
    # ------------------------------------------------------------------ #

    def test_primary_color_descendants_includes_red(self) -> None:
        self.assertIn('Red', self.finder.descendants('Primary_Color'))

    def test_color_descendants_includes_red(self) -> None:
        self.assertIn('Red', self.finder.descendants('Color'))

    def test_color_descendants_includes_teal(self) -> None:
        self.assertIn('Teal', self.finder.descendants('Color'))

    def test_color_descendants_includes_silver(self) -> None:
        self.assertIn('Silver', self.finder.descendants('Color'))


if __name__ == '__main__':
    unittest.main()
