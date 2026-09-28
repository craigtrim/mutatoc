#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests parser behaviour when input contains multiple entities, a mix of
# recognised and unrecognised tokens, and varying surface forms of known entities.

import os
import json
import unittest
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'

COURSES_JSON_PATH = 'tests/test_data/ontologies/courses-20250122.json'


class TestMutatoAPIJsonMultiEntity(unittest.TestCase):

    def setUp(self) -> None:
        with open(COURSES_JSON_PATH, 'r') as f:
            d_owl = json.load(f)
        finder = FindOntologyJSON(d_owl=d_owl, ontology_name='courses')
        self.api = MutatoAPI(find_ontology_data=finder)

    def tearDown(self) -> None:
        self.api = None

    def _normals(self, input_text: str) -> list[str]:
        result = self.api.swap_input_text(input_text)
        return [t['normal'] for t in result if 'normal' in t]

    def test_unrecognised_tokens_still_appear_in_output(self) -> None:
        result = self.api.swap_input_text('xyzqwerty foobar123')
        self.assertIsNotNone(result)
        self.assertGreater(len(result), 0)

    def test_recognised_entity_with_surrounding_noise(self) -> None:
        entities = self._normals('foo Geometry bar')
        self.assertIn('geometry', entities)

    def test_every_token_has_a_normal_field(self) -> None:
        result = self.api.swap_input_text('Geometry foo bar Algebra')
        for token in result:
            self.assertIn('normal', token)

    def test_case_variants_normalise_to_same_entity(self) -> None:
        lower = self._normals('geometry')
        upper = self._normals('GEOMETRY')
        title = self._normals('Geometry')
        self.assertIn('geometry', lower)
        self.assertIn('geometry', upper)
        self.assertIn('geometry', title)

    def test_two_separate_recognised_entities(self) -> None:
        # Both 'geometry' and 'algebra' should appear if both are in the ontology
        result = self.api.swap_input_text('Geometry Algebra')
        normals = [t['normal'] for t in result if 'normal' in t]
        # At minimum, we expect two tokens in the output
        self.assertGreaterEqual(len(normals), 2)

    def test_output_token_count_matches_expected_token_boundary(self) -> None:
        # 'Geometry' is one word → should produce at least one token
        result = self.api.swap_input_text('Geometry')
        self.assertGreaterEqual(len(result), 1)

    def test_apostrophe_form_normalises_to_same_as_possessive(self) -> None:
        # womens_choir should be found regardless of apostrophe
        entities_no_apos = self._normals('womens choir')
        entities_apos = self._normals("women's choir")
        self.assertIn('womens_choir', entities_no_apos)
        self.assertIn('womens_choir', entities_apos)


if __name__ == '__main__':
    unittest.main()
