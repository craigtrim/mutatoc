#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests exact-match and altLabel-match behaviour against the colors-test ontology.
# Covers primary, secondary, neutral, warm, cool, and metallic color families,
# plus multi-word span entities like "Primary Color" and "Neutral Color".

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'colors-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/colors'


class TestColorsParserBasic(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='colors')
        cls.api = MutatoAPI(find_ontology_data=cls.finder)

    def _swaps(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['swaps'] for t in result if t.get('swaps')]

    def _canons(self, text: str) -> list:
        return [s['canon'] for s in self._swaps(text)]

    def _normals(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['normal'] for t in result if 'normal' in t]

    # ------------------------------------------------------------------ #
    # Primary colors                                                       #
    # ------------------------------------------------------------------ #

    def test_red_exact_match(self) -> None:
        self.assertIn('red', self._canons('Red'))

    def test_blue_exact_match(self) -> None:
        self.assertIn('blue', self._canons('Blue'))

    def test_yellow_exact_match(self) -> None:
        self.assertIn('yellow', self._canons('Yellow'))

    # ------------------------------------------------------------------ #
    # Secondary colors                                                     #
    # ------------------------------------------------------------------ #

    def test_orange_exact_match(self) -> None:
        self.assertIn('orange', self._canons('Orange'))

    def test_green_exact_match(self) -> None:
        self.assertIn('green', self._canons('Green'))

    def test_purple_exact_match(self) -> None:
        self.assertIn('purple', self._canons('Purple'))

    def test_indigo_exact_match(self) -> None:
        self.assertIn('indigo', self._canons('Indigo'))

    # ------------------------------------------------------------------ #
    # Neutral colors                                                       #
    # ------------------------------------------------------------------ #

    def test_black_exact_match(self) -> None:
        self.assertIn('black', self._canons('Black'))

    def test_white_exact_match(self) -> None:
        self.assertIn('white', self._canons('White'))

    def test_gray_exact_match(self) -> None:
        self.assertIn('gray', self._canons('Gray'))

    def test_brown_exact_match(self) -> None:
        self.assertIn('brown', self._canons('Brown'))

    def test_beige_exact_match(self) -> None:
        self.assertIn('beige', self._canons('Beige'))

    # ------------------------------------------------------------------ #
    # Warm colors                                                          #
    # ------------------------------------------------------------------ #

    def test_coral_exact_match(self) -> None:
        self.assertIn('coral', self._canons('Coral'))

    def test_pink_exact_match(self) -> None:
        self.assertIn('pink', self._canons('Pink'))

    def test_maroon_exact_match(self) -> None:
        self.assertIn('maroon', self._canons('Maroon'))

    # ------------------------------------------------------------------ #
    # Cool colors                                                          #
    # ------------------------------------------------------------------ #

    def test_teal_exact_match(self) -> None:
        self.assertIn('teal', self._canons('Teal'))

    def test_turquoise_exact_match(self) -> None:
        self.assertIn('turquoise', self._canons('Turquoise'))

    def test_navy_exact_match(self) -> None:
        self.assertIn('navy', self._canons('Navy'))

    def test_mint_exact_match(self) -> None:
        self.assertIn('mint', self._canons('Mint'))

    # ------------------------------------------------------------------ #
    # Metallic colors                                                      #
    # ------------------------------------------------------------------ #

    def test_silver_exact_match(self) -> None:
        self.assertIn('silver', self._canons('Silver'))

    def test_bronze_exact_match(self) -> None:
        self.assertIn('bronze', self._canons('Bronze'))

    # ------------------------------------------------------------------ #
    # altLabel matching                                                    #
    # ------------------------------------------------------------------ #

    def test_crimson_maps_to_red(self) -> None:
        self.assertIn('red', self._canons('crimson'))

    def test_scarlet_maps_to_red(self) -> None:
        self.assertIn('red', self._canons('scarlet'))

    def test_azure_maps_to_blue(self) -> None:
        self.assertIn('blue', self._canons('azure'))

    def test_cobalt_maps_to_blue(self) -> None:
        self.assertIn('blue', self._canons('cobalt'))

    def test_emerald_maps_to_green(self) -> None:
        self.assertIn('green', self._canons('emerald'))

    def test_violet_maps_to_purple(self) -> None:
        self.assertIn('purple', self._canons('violet'))

    def test_lavender_maps_to_purple(self) -> None:
        self.assertIn('purple', self._canons('lavender'))

    def test_grey_maps_to_gray(self) -> None:
        self.assertIn('gray', self._canons('grey'))

    def test_tan_maps_to_brown(self) -> None:
        self.assertIn('brown', self._canons('tan'))

    def test_chocolate_maps_to_brown(self) -> None:
        self.assertIn('brown', self._canons('chocolate'))

    def test_rose_maps_to_pink(self) -> None:
        self.assertIn('pink', self._canons('rose'))

    def test_magenta_maps_to_pink(self) -> None:
        self.assertIn('pink', self._canons('magenta'))

    def test_cyan_maps_to_teal(self) -> None:
        self.assertIn('teal', self._canons('cyan'))

    def test_burgundy_maps_to_maroon(self) -> None:
        self.assertIn('maroon', self._canons('burgundy'))

    def test_tangerine_maps_to_orange(self) -> None:
        self.assertIn('orange', self._canons('tangerine'))

    def test_chrome_maps_to_silver(self) -> None:
        self.assertIn('silver', self._canons('chrome'))

    def test_colour_maps_to_color(self) -> None:
        self.assertIn('color', self._canons('colour'))

    # ------------------------------------------------------------------ #
    # Multi-word span matches                                              #
    # ------------------------------------------------------------------ #

    def test_primary_color_span(self) -> None:
        swaps = self._swaps('Primary Color')
        self.assertGreater(len(swaps), 0)

    def test_secondary_color_span(self) -> None:
        swaps = self._swaps('Secondary Color')
        self.assertGreater(len(swaps), 0)

    def test_neutral_color_span(self) -> None:
        swaps = self._swaps('Neutral Color')
        self.assertGreater(len(swaps), 0)

    def test_warm_color_span(self) -> None:
        swaps = self._swaps('Warm Color')
        self.assertGreater(len(swaps), 0)

    def test_cool_color_span(self) -> None:
        swaps = self._swaps('Cool Color')
        self.assertGreater(len(swaps), 0)

    def test_metallic_color_span(self) -> None:
        swaps = self._swaps('Metallic Color')
        self.assertGreater(len(swaps), 0)

    # ------------------------------------------------------------------ #
    # Edge cases                                                           #
    # ------------------------------------------------------------------ #

    def test_unknown_color_returns_list(self) -> None:
        result = self.api.swap_input_text('zzxqfoo')
        self.assertIsInstance(result, list)

    def test_unknown_color_no_swaps(self) -> None:
        self.assertEqual(len(self._swaps('zzxqfoo')), 0)

    def test_case_invariant_red(self) -> None:
        self.assertIn('red', self._canons('RED'))
        self.assertIn('red', self._canons('red'))
        self.assertIn('red', self._canons('Red'))

    def test_swap_type_is_known_value(self) -> None:
        valid = {'exact', 'span', 'hierarchy'}
        result = self.api.swap_input_text('Red Blue Green')
        for token in result:
            if token.get('swaps'):
                self.assertIn(token['swaps']['type'], valid)


if __name__ == '__main__':
    unittest.main()
