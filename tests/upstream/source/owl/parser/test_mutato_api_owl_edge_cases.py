#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests parser robustness: None, empty string, whitespace, numerics, all-caps.
# These are the inputs that are most likely to surface unexpected behaviour.

import os
import unittest
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyData

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPIOwlEdgeCases(unittest.TestCase):

    def setUp(self) -> None:
        finder = FindOntologyData(
            ontologies=['medic-copilot-20230726'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )
        self.api = MutatoAPI(find_ontology_data=finder)

    def tearDown(self) -> None:
        self.api = None

    def test_none_input_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(None))

    def test_empty_string_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(''))

    def test_whitespace_only_does_not_raise(self) -> None:
        try:
            result = self.api.swap_input_text('   ')
            self.assertTrue(result is None or isinstance(result, list))
        except Exception as e:
            self.fail(f"Unexpected exception for whitespace input: {e}")

    def test_numeric_string_returns_list(self) -> None:
        result = self.api.swap_input_text('123')
        self.assertIsNotNone(result)
        self.assertIsInstance(result, list)

    def test_all_caps_returns_list(self) -> None:
        result = self.api.swap_input_text('AWS DATA AI')
        self.assertIsNotNone(result)
        self.assertIsInstance(result, list)

    def test_single_character_returns_list(self) -> None:
        result = self.api.swap_input_text('a')
        self.assertIsNotNone(result)
        self.assertIsInstance(result, list)

    def test_punctuation_only_does_not_raise(self) -> None:
        try:
            result = self.api.swap_input_text('!!! ???')
            self.assertTrue(result is None or isinstance(result, list))
        except Exception as e:
            self.fail(f"Unexpected exception for punctuation input: {e}")

    def test_mixed_alphanumeric_returns_list(self) -> None:
        result = self.api.swap_input_text('abc123 def456')
        self.assertIsNotNone(result)
        self.assertIsInstance(result, list)


if __name__ == '__main__':
    unittest.main()
