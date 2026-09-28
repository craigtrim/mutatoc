#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests multi-word span matching against the animals-test ontology.
# Entities whose OWL labels contain spaces generate span entries; this
# file verifies they are found both in isolation and inside sentences.

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'animals-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/animals'


class TestAnimalsParserSpans(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='animals')
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

    def _has_span_swap(self, text: str) -> bool:
        return any(s['type'] == 'span' for s in self._swaps(text))

    # ------------------------------------------------------------------ #
    # Span match  - isolation (span is the entire input)                   #
    # ------------------------------------------------------------------ #

    def test_german_shepherd_produces_swap(self) -> None:
        swaps = self._swaps('German Shepherd')
        self.assertGreater(len(swaps), 0)

    def test_german_shepherd_canon(self) -> None:
        self.assertIn('german_shepherd', self._canons('German Shepherd'))

    def test_blue_whale_produces_swap(self) -> None:
        swaps = self._swaps('Blue Whale')
        self.assertGreater(len(swaps), 0)

    def test_blue_whale_canon(self) -> None:
        self.assertIn('blue_whale', self._canons('Blue Whale'))

    def test_maine_coon_produces_swap(self) -> None:
        swaps = self._swaps('Maine Coon')
        self.assertGreater(len(swaps), 0)

    def test_maine_coon_canon(self) -> None:
        self.assertIn('maine_coon', self._canons('Maine Coon'))

    def test_persian_cat_produces_swap(self) -> None:
        swaps = self._swaps('Persian Cat')
        self.assertGreater(len(swaps), 0)

    def test_polar_bear_produces_swap(self) -> None:
        swaps = self._swaps('Polar Bear')
        self.assertGreater(len(swaps), 0)

    def test_polar_bear_canon(self) -> None:
        self.assertIn('polar_bear', self._canons('Polar Bear'))

    def test_brown_bear_produces_swap(self) -> None:
        swaps = self._swaps('Brown Bear')
        self.assertGreater(len(swaps), 0)

    def test_african_elephant_produces_swap(self) -> None:
        swaps = self._swaps('African Elephant')
        self.assertGreater(len(swaps), 0)

    def test_african_elephant_canon(self) -> None:
        self.assertIn('african_elephant', self._canons('African Elephant'))

    def test_asian_elephant_produces_swap(self) -> None:
        swaps = self._swaps('Asian Elephant')
        self.assertGreater(len(swaps), 0)

    def test_arabian_horse_produces_swap(self) -> None:
        swaps = self._swaps('Arabian Horse')
        self.assertGreater(len(swaps), 0)

    def test_bald_eagle_produces_swap(self) -> None:
        swaps = self._swaps('Bald Eagle')
        self.assertGreater(len(swaps), 0)

    def test_bald_eagle_canon(self) -> None:
        self.assertIn('bald_eagle', self._canons('Bald Eagle'))

    def test_golden_eagle_produces_swap(self) -> None:
        swaps = self._swaps('Golden Eagle')
        self.assertGreater(len(swaps), 0)

    def test_great_white_shark_produces_swap(self) -> None:
        swaps = self._swaps('Great White Shark')
        self.assertGreater(len(swaps), 0)

    def test_great_white_shark_canon(self) -> None:
        self.assertIn('great_white_shark', self._canons('Great White Shark'))

    def test_hammerhead_shark_produces_swap(self) -> None:
        swaps = self._swaps('Hammerhead Shark')
        self.assertGreater(len(swaps), 0)

    def test_humpback_whale_produces_swap(self) -> None:
        swaps = self._swaps('Humpback Whale')
        self.assertGreater(len(swaps), 0)

    def test_monarch_butterfly_produces_swap(self) -> None:
        swaps = self._swaps('Monarch Butterfly')
        self.assertGreater(len(swaps), 0)

    # ------------------------------------------------------------------ #
    # Span match  - entity embedded in a sentence                          #
    # ------------------------------------------------------------------ #

    def test_german_shepherd_in_sentence(self) -> None:
        canons = self._canons('I have a German Shepherd dog')
        self.assertIn('german_shepherd', canons)

    def test_blue_whale_in_sentence(self) -> None:
        canons = self._canons('The Blue Whale is enormous')
        self.assertIn('blue_whale', canons)

    def test_great_white_shark_in_sentence(self) -> None:
        canons = self._canons('A Great White Shark appeared')
        self.assertIn('great_white_shark', canons)

    def test_african_elephant_in_sentence(self) -> None:
        canons = self._canons('We spotted an African Elephant')
        self.assertIn('african_elephant', canons)

    def test_polar_bear_in_sentence(self) -> None:
        canons = self._canons('The Polar Bear swam north')
        self.assertIn('polar_bear', canons)

    def test_bald_eagle_in_sentence(self) -> None:
        canons = self._canons('A Bald Eagle soared overhead')
        self.assertIn('bald_eagle', canons)


if __name__ == '__main__':
    unittest.main()
