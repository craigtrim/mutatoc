#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Verifies that repeated calls with identical input always produce identical output.
# Also checks that the ctr (recursion counter) parameter does not change results
# for well-established entities  - only that recursion stops at ctr >= 2.

import os
import json
import unittest
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'

COURSES_JSON_PATH = 'tests/test_data/ontologies/courses-20250122.json'
INPUT_TEXT = 'womens choir'


class TestMutatoAPIJsonIdempotency(unittest.TestCase):

    def setUp(self) -> None:
        with open(COURSES_JSON_PATH, 'r') as f:
            d_owl = json.load(f)
        finder = FindOntologyJSON(d_owl=d_owl, ontology_name='courses')
        self.api = MutatoAPI(find_ontology_data=finder)

    def tearDown(self) -> None:
        self.api = None

    def test_same_input_produces_same_output(self) -> None:
        result1 = self.api.swap_input_text(INPUT_TEXT)
        result2 = self.api.swap_input_text(INPUT_TEXT)
        self.assertEqual(result1, result2)

    def test_repeated_calls_find_same_entities(self) -> None:
        for _ in range(3):
            result = self.api.swap_input_text(INPUT_TEXT)
            entities = [t['normal'] for t in result if 'normal' in t]
            self.assertIn('womens_choir', entities)

    def test_result_token_count_is_stable(self) -> None:
        result1 = self.api.swap_input_text(INPUT_TEXT)
        result2 = self.api.swap_input_text(INPUT_TEXT)
        self.assertEqual(len(result1), len(result2))

    def test_token_order_is_stable(self) -> None:
        result1 = self.api.swap_input_text(INPUT_TEXT)
        result2 = self.api.swap_input_text(INPUT_TEXT)
        for t1, t2 in zip(result1, result2):
            self.assertEqual(t1.get('normal'), t2.get('normal'))

    def test_high_ctr_still_finds_entity(self) -> None:
        # ctr=100 prevents recursion; entity should still match on first pass
        result = self.api.swap_input_text(INPUT_TEXT, ctr=100)
        entities = [t['normal'] for t in result if 'normal' in t]
        self.assertIn('womens_choir', entities)


if __name__ == '__main__':
    unittest.main()
