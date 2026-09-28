#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md
# Tests OntologyParser(owl_path) — the OWL-backed construction path.
# Uses courses-20250122.owl. These tests are slower (OWL load + MDA build).

import json
import os
import unittest

os.environ['SPAN_DISTANCE'] = '4'

COURSES_OWL = 'tests/test_data/ontologies/courses-20250122.owl'
COURSES_JSON = 'tests/test_data/ontologies/courses-20250122.json'


class TestOntologyParserFromOwl(unittest.TestCase):
    """OntologyParser(owl_path) construction and contract."""

    @classmethod
    def setUpClass(cls) -> None:
        from mutato.api import OntologyParser
        cls.parser = OntologyParser(COURSES_OWL)

    # ------------------------------------------------------------------ #
    # Construction                                                         #
    # ------------------------------------------------------------------ #

    def test_parser_is_not_none(self) -> None:
        self.assertIsNotNone(self.parser)

    # ------------------------------------------------------------------ #
    # to_dict                                                              #
    # ------------------------------------------------------------------ #

    def test_to_dict_returns_dict(self) -> None:
        self.assertIsInstance(self.parser.to_dict(), dict)

    def test_to_dict_is_non_empty(self) -> None:
        self.assertGreater(len(self.parser.to_dict()), 0)

    def test_to_dict_contains_synonyms(self) -> None:
        self.assertIn('synonyms', self.parser.to_dict())

    def test_to_dict_is_json_serialisable(self) -> None:
        serialised = json.dumps(self.parser.to_dict())
        self.assertIsInstance(serialised, str)

    # ------------------------------------------------------------------ #
    # parse                                                                #
    # ------------------------------------------------------------------ #

    def test_parse_returns_string(self) -> None:
        self.assertIsInstance(self.parser.parse('Geometry'), str)

    def test_geometry_normalises(self) -> None:
        self.assertEqual(self.parser.parse('Geometry'), 'geometry')

    def test_abstract_algebra_normalises(self) -> None:
        result = self.parser.parse('abstract algebra')
        self.assertEqual(result, 'abstract_algebra')

    # ------------------------------------------------------------------ #
    # Parity: OWL path and dict path produce the same parse result        #
    # ------------------------------------------------------------------ #

    def test_owl_and_dict_paths_produce_same_result(self) -> None:
        from mutato.api import OntologyParser

        with open(COURSES_JSON) as f:
            d = json.load(f)
        dict_parser = OntologyParser.from_dict(d, name='courses')

        sentences = [
            'Geometry',
            'abstract algebra',
            'I studied abnormal psychology last semester',
            'blah blah nothing here',
        ]
        for text in sentences:
            with self.subTest(text=text):
                self.assertEqual(
                    self.parser.parse(text),
                    dict_parser.parse(text),
                )


if __name__ == '__main__':
    unittest.main()
