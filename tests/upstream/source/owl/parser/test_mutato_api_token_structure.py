#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Inspects the structure of individual tokens returned by swap_input_text.
# Uses 'Geometry' against the courses ontology since its exact-match behaviour
# is already established: canon='geometry', type='exact'.

import os
import json
import unittest
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'

COURSES_JSON_PATH = 'tests/test_data/ontologies/courses-20250122.json'


class TestMutatoAPITokenStructure(unittest.TestCase):

    def setUp(self) -> None:
        with open(COURSES_JSON_PATH, 'r') as f:
            d_owl = json.load(f)
        finder = FindOntologyJSON(d_owl=d_owl, ontology_name='courses')
        self.api = MutatoAPI(find_ontology_data=finder)

    def tearDown(self) -> None:
        self.api = None

    def test_result_is_a_list(self) -> None:
        result = self.api.swap_input_text('Geometry')
        self.assertIsInstance(result, list)

    def test_each_token_is_a_dict(self) -> None:
        result = self.api.swap_input_text('Geometry')
        for token in result:
            self.assertIsInstance(token, dict)

    def test_all_tokens_have_normal_field(self) -> None:
        result = self.api.swap_input_text('Geometry foo bar')
        for token in result:
            self.assertIn('normal', token, f"Token missing 'normal': {token}")

    def test_normal_field_is_a_string(self) -> None:
        result = self.api.swap_input_text('Geometry foo bar')
        for token in result:
            self.assertIsInstance(token['normal'], str)

    def test_normal_field_is_lowercase(self) -> None:
        result = self.api.swap_input_text('GEOMETRY FOO BAR')
        for token in result:
            self.assertEqual(token['normal'], token['normal'].lower())

    def test_swapped_token_has_swaps_field(self) -> None:
        result = self.api.swap_input_text('Geometry')
        swapped = [t for t in result if t.get('swaps')]
        self.assertGreater(len(swapped), 0, "Expected at least one swapped token")

    def test_geometry_swap_canon_is_correct(self) -> None:
        result = self.api.swap_input_text('Geometry')
        self.assertEqual(result[0]['swaps']['canon'], 'geometry')

    def test_geometry_swap_type_is_exact(self) -> None:
        result = self.api.swap_input_text('Geometry')
        self.assertEqual(result[0]['swaps']['type'], 'exact')

    def test_swap_type_is_a_known_value(self) -> None:
        result = self.api.swap_input_text('Geometry foo bar')
        valid_types = {'exact', 'span', 'hierarchy'}
        for token in result:
            if token.get('swaps'):
                self.assertIn(token['swaps'].get('type'), valid_types)

    def test_swap_canon_is_a_string(self) -> None:
        result = self.api.swap_input_text('Geometry')
        for token in result:
            if token.get('swaps'):
                self.assertIsInstance(token['swaps'].get('canon'), str)


if __name__ == '__main__':
    unittest.main()
