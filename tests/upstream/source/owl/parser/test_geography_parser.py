#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests exact, altLabel, and span matching against the geography-test ontology.
# Exercises countries, cities, continents, landmarks, and oceans  - including
# common abbreviations (USA, UK, NYC, LA) and multi-word geographic spans.

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'geography-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/geography'


class TestGeographyParser(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='geography')
        cls.api = MutatoAPI(find_ontology_data=cls.finder)

    def _swaps(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['swaps'] for t in result if t.get('swaps')]

    def _canons(self, text: str) -> list:
        return [s['canon'] for s in self._swaps(text)]

    # ------------------------------------------------------------------ #
    # Countries  - exact match                                              #
    # ------------------------------------------------------------------ #

    def test_france_exact_match(self) -> None:
        self.assertIn('france', self._canons('France'))

    def test_germany_exact_match(self) -> None:
        self.assertIn('germany', self._canons('Germany'))

    def test_japan_exact_match(self) -> None:
        self.assertIn('japan', self._canons('Japan'))

    def test_china_exact_match(self) -> None:
        self.assertIn('china', self._canons('China'))

    def test_brazil_exact_match(self) -> None:
        self.assertIn('brazil', self._canons('Brazil'))

    def test_canada_exact_match(self) -> None:
        self.assertIn('canada', self._canons('Canada'))

    def test_mexico_exact_match(self) -> None:
        self.assertIn('mexico', self._canons('Mexico'))

    def test_italy_exact_match(self) -> None:
        self.assertIn('italy', self._canons('Italy'))

    def test_spain_exact_match(self) -> None:
        self.assertIn('spain', self._canons('Spain'))

    def test_india_exact_match(self) -> None:
        self.assertIn('india', self._canons('India'))

    def test_argentina_exact_match(self) -> None:
        self.assertIn('argentina', self._canons('Argentina'))

    # ------------------------------------------------------------------ #
    # Cities  - exact match                                                 #
    # ------------------------------------------------------------------ #

    def test_london_exact_match(self) -> None:
        self.assertIn('london', self._canons('London'))

    def test_paris_exact_match(self) -> None:
        self.assertIn('paris', self._canons('Paris'))

    def test_tokyo_exact_match(self) -> None:
        self.assertIn('tokyo', self._canons('Tokyo'))

    def test_beijing_exact_match(self) -> None:
        self.assertIn('beijing', self._canons('Beijing'))

    def test_chicago_exact_match(self) -> None:
        self.assertIn('chicago', self._canons('Chicago'))

    def test_sydney_exact_match(self) -> None:
        self.assertIn('sydney', self._canons('Sydney'))

    def test_berlin_exact_match(self) -> None:
        self.assertIn('berlin', self._canons('Berlin'))

    def test_rome_exact_match(self) -> None:
        self.assertIn('rome', self._canons('Rome'))

    # ------------------------------------------------------------------ #
    # Continents  - exact match                                             #
    # ------------------------------------------------------------------ #

    def test_europe_exact_match(self) -> None:
        self.assertIn('europe', self._canons('Europe'))

    def test_asia_exact_match(self) -> None:
        self.assertIn('asia', self._canons('Asia'))

    def test_africa_exact_match(self) -> None:
        self.assertIn('africa', self._canons('Africa'))

    def test_antarctica_exact_match(self) -> None:
        self.assertIn('antarctica', self._canons('Antarctica'))

    # ------------------------------------------------------------------ #
    # altLabel matching                                                    #
    # ------------------------------------------------------------------ #

    def test_usa_maps_to_united_states(self) -> None:
        self.assertIn('united_states', self._canons('USA'))

    def test_america_maps_to_united_states(self) -> None:
        self.assertIn('united_states', self._canons('America'))

    def test_uk_maps_to_united_kingdom(self) -> None:
        self.assertIn('united_kingdom', self._canons('UK'))

    def test_britain_maps_to_united_kingdom(self) -> None:
        self.assertIn('united_kingdom', self._canons('Britain'))

    def test_england_maps_to_united_kingdom(self) -> None:
        self.assertIn('united_kingdom', self._canons('England'))

    def test_nyc_maps_to_new_york(self) -> None:
        self.assertIn('new_york', self._canons('NYC'))

    def test_la_maps_to_los_angeles(self) -> None:
        self.assertIn('los_angeles', self._canons('LA'))

    def test_deutschland_maps_to_germany(self) -> None:
        self.assertIn('germany', self._canons('Deutschland'))

    def test_prc_maps_to_china(self) -> None:
        self.assertIn('china', self._canons('PRC'))

    def test_peking_maps_to_beijing(self) -> None:
        self.assertIn('beijing', self._canons('peking'))

    def test_bombay_maps_to_mumbai(self) -> None:
        self.assertIn('mumbai', self._canons('bombay'))

    def test_everest_maps_to_mount_everest(self) -> None:
        self.assertIn('mount_everest', self._canons('everest'))

    # ------------------------------------------------------------------ #
    # Multi-word span matches                                              #
    # ------------------------------------------------------------------ #

    def test_north_america_span(self) -> None:
        self.assertIn('north_america', self._canons('North America'))

    def test_south_america_span(self) -> None:
        self.assertIn('south_america', self._canons('South America'))

    def test_united_states_span(self) -> None:
        self.assertIn('united_states', self._canons('United States'))

    def test_united_kingdom_span(self) -> None:
        self.assertIn('united_kingdom', self._canons('United Kingdom'))

    def test_new_york_span(self) -> None:
        self.assertIn('new_york', self._canons('New York'))

    def test_los_angeles_span(self) -> None:
        self.assertIn('los_angeles', self._canons('Los Angeles'))

    def test_south_africa_span(self) -> None:
        self.assertIn('south_africa', self._canons('South Africa'))

    def test_eiffel_tower_span(self) -> None:
        self.assertIn('eiffel_tower', self._canons('Eiffel Tower'))

    def test_great_wall_span(self) -> None:
        self.assertIn('great_wall', self._canons('Great Wall'))

    def test_pacific_ocean_span(self) -> None:
        self.assertIn('pacific_ocean', self._canons('Pacific Ocean'))

    def test_mount_everest_span(self) -> None:
        self.assertIn('mount_everest', self._canons('Mount Everest'))

    def test_big_ben_span(self) -> None:
        self.assertIn('big_ben', self._canons('Big Ben'))

    # ------------------------------------------------------------------ #
    # Embedded in sentence                                                 #
    # ------------------------------------------------------------------ #

    def test_united_states_in_sentence(self) -> None:
        self.assertIn('united_states', self._canons('I visited the United States last year'))

    def test_new_york_in_sentence(self) -> None:
        self.assertIn('new_york', self._canons('She lives in New York'))


if __name__ == '__main__':
    unittest.main()
