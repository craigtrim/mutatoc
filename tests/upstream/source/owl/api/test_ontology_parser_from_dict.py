#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md
# Tests OntologyParser.from_dict() — the fast (no OWL) construction path.
# Uses the pre-built courses-20250122.json as the backing dict.

import json
import os
import unittest

os.environ['SPAN_DISTANCE'] = '4'

COURSES_JSON = 'tests/test_data/ontologies/courses-20250122.json'


def _load_courses() -> dict:
    with open(COURSES_JSON) as f:
        return json.load(f)


class TestOntologyParserFromDict(unittest.TestCase):
    """OntologyParser.from_dict() construction and basic contract."""

    def setUp(self) -> None:
        from mutato.api import OntologyParser
        self.parser = OntologyParser.from_dict(_load_courses(), name='courses')

    def tearDown(self) -> None:
        self.parser = None

    # ------------------------------------------------------------------ #
    # Construction                                                         #
    # ------------------------------------------------------------------ #

    def test_parser_is_not_none(self) -> None:
        self.assertIsNotNone(self.parser)

    def test_parser_has_parse_method(self) -> None:
        self.assertTrue(callable(getattr(self.parser, 'parse', None)))

    def test_parser_has_to_dict_method(self) -> None:
        self.assertTrue(callable(getattr(self.parser, 'to_dict', None)))

    # ------------------------------------------------------------------ #
    # to_dict                                                              #
    # ------------------------------------------------------------------ #

    def test_to_dict_returns_dict(self) -> None:
        self.assertIsInstance(self.parser.to_dict(), dict)

    def test_to_dict_is_non_empty(self) -> None:
        self.assertGreater(len(self.parser.to_dict()), 0)

    def test_to_dict_contains_synonyms(self) -> None:
        self.assertIn('synonyms', self.parser.to_dict())

    def test_to_dict_contains_ngrams(self) -> None:
        self.assertIn('ngrams', self.parser.to_dict())

    def test_to_dict_contains_spans(self) -> None:
        self.assertIn('spans', self.parser.to_dict())

    def test_to_dict_is_json_serialisable(self) -> None:
        serialised = json.dumps(self.parser.to_dict())
        self.assertIsInstance(serialised, str)
        self.assertGreater(len(serialised), 0)

    # ------------------------------------------------------------------ #
    # parse — return type                                                  #
    # ------------------------------------------------------------------ #

    def test_parse_returns_string(self) -> None:
        self.assertIsInstance(self.parser.parse('Geometry'), str)

    def test_parse_returns_non_empty_string_for_non_empty_input(self) -> None:
        result = self.parser.parse('Geometry')
        self.assertGreater(len(result), 0)

    def test_parse_empty_string_returns_empty(self) -> None:
        result = self.parser.parse('')
        self.assertEqual(result, '')

    # ------------------------------------------------------------------ #
    # parse — exact single-word match                                      #
    # ------------------------------------------------------------------ #

    def test_geometry_normalises_to_canonical(self) -> None:
        result = self.parser.parse('Geometry')
        self.assertEqual(result, 'geometry')

    def test_geometry_case_insensitive(self) -> None:
        self.assertEqual(self.parser.parse('GEOMETRY'), 'geometry')

    def test_anova_normalises_to_canonical(self) -> None:
        result = self.parser.parse('anova')
        self.assertEqual(result, 'anova')

    def test_inflection_normalises_to_canonical(self) -> None:
        result = self.parser.parse('inflection')
        self.assertEqual(result, 'inflection')

    # ------------------------------------------------------------------ #
    # parse — multi-word exact match (two-gram)                            #
    # ------------------------------------------------------------------ #

    def test_abstract_algebra_normalises(self) -> None:
        result = self.parser.parse('abstract algebra')
        self.assertEqual(result, 'abstract_algebra')

    def test_abnormal_psychology_normalises(self) -> None:
        result = self.parser.parse('abnormal psychology')
        self.assertEqual(result, 'abnormal_psychology')

    def test_academic_english_normalises(self) -> None:
        result = self.parser.parse('academic english')
        self.assertEqual(result, 'academic_english')

    # ------------------------------------------------------------------ #
    # parse — mixed (matched + unmatched tokens)                           #
    # ------------------------------------------------------------------ #

    def test_mixed_sentence_preserves_unmatched_tokens(self) -> None:
        result = self.parser.parse('I studied Geometry today')
        self.assertIn('geometry', result)
        self.assertIn('I', result)

    def test_mixed_sentence_with_multiword(self) -> None:
        result = self.parser.parse('I took abstract algebra last year')
        self.assertIn('abstract_algebra', result)
        self.assertIn('I', result)
        self.assertIn('last', result)

    def test_unmatched_text_returned_unchanged(self) -> None:
        result = self.parser.parse('blah blah xyz nonsense')
        # none of these are in the ontology — all tokens should come back
        self.assertIn('blah', result)
        self.assertIn('xyz', result)

    # ------------------------------------------------------------------ #
    # parse — idempotency                                                  #
    # ------------------------------------------------------------------ #

    def test_parse_is_idempotent_on_canonical_form(self) -> None:
        first = self.parser.parse('Geometry')
        second = self.parser.parse(first)
        self.assertEqual(first, second)

    def test_parse_called_twice_gives_same_result(self) -> None:
        a = self.parser.parse('I studied abstract algebra')
        b = self.parser.parse('I studied abstract algebra')
        self.assertEqual(a, b)

    # ------------------------------------------------------------------ #
    # parse — multiple terms in one sentence                               #
    # ------------------------------------------------------------------ #

    def test_two_matched_terms_in_one_sentence(self) -> None:
        result = self.parser.parse('Geometry and abstract algebra')
        self.assertIn('geometry', result)
        self.assertIn('abstract_algebra', result)


class TestOntologyParserRoundTrip(unittest.TestCase):
    """OntologyParser round-trip: from_dict -> to_dict -> from_dict."""

    def test_round_trip_produces_same_parse_result(self) -> None:
        from mutato.api import OntologyParser

        d_original = _load_courses()
        p1 = OntologyParser.from_dict(d_original, name='courses')
        d_exported = p1.to_dict()

        p2 = OntologyParser.from_dict(d_exported, name='courses')

        self.assertEqual(
            p1.parse('I studied abstract algebra and Geometry'),
            p2.parse('I studied abstract algebra and Geometry'),
        )

    def test_to_dict_output_equals_input(self) -> None:
        from mutato.api import OntologyParser

        d = _load_courses()
        p = OntologyParser.from_dict(d, name='courses')
        self.assertEqual(p.to_dict(), d)


if __name__ == '__main__':
    unittest.main()
