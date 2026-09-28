#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests exact, altLabel, and span matching for musical genres and abstract
# music concepts (melody, harmony, rhythm, etc.) against music-test ontology.

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'music-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/music'


class TestMusicParserGenres(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='music')
        cls.api = MutatoAPI(find_ontology_data=cls.finder)

    def _swaps(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['swaps'] for t in result if t.get('swaps')]

    def _canons(self, text: str) -> list:
        return [s['canon'] for s in self._swaps(text)]

    # ------------------------------------------------------------------ #
    # Genre  - exact match                                                  #
    # ------------------------------------------------------------------ #

    def test_jazz_exact_match(self) -> None:
        self.assertIn('jazz', self._canons('Jazz'))

    def test_blues_exact_match(self) -> None:
        self.assertIn('blues', self._canons('Blues'))

    def test_opera_exact_match(self) -> None:
        self.assertIn('opera', self._canons('Opera'))

    def test_reggae_exact_match(self) -> None:
        self.assertIn('reggae', self._canons('Reggae'))

    def test_soul_exact_match(self) -> None:
        self.assertIn('soul', self._canons('Soul'))

    def test_bebop_exact_match(self) -> None:
        self.assertIn('bebop', self._canons('Bebop'))

    def test_baroque_exact_match(self) -> None:
        self.assertIn('baroque', self._canons('Baroque'))

    def test_romantic_exact_match(self) -> None:
        self.assertIn('romantic', self._canons('Romantic'))

    # ------------------------------------------------------------------ #
    # Concepts  - exact match                                               #
    # ------------------------------------------------------------------ #

    def test_melody_exact_match(self) -> None:
        self.assertIn('melody', self._canons('Melody'))

    def test_harmony_exact_match(self) -> None:
        self.assertIn('harmony', self._canons('Harmony'))

    def test_rhythm_exact_match(self) -> None:
        self.assertIn('rhythm', self._canons('Rhythm'))

    def test_tempo_exact_match(self) -> None:
        self.assertIn('tempo', self._canons('Tempo'))

    def test_chord_exact_match(self) -> None:
        self.assertIn('chord', self._canons('Chord'))

    def test_scale_exact_match(self) -> None:
        self.assertIn('scale', self._canons('Scale'))

    def test_pitch_exact_match(self) -> None:
        self.assertIn('pitch', self._canons('Pitch'))

    def test_timbre_exact_match(self) -> None:
        self.assertIn('timbre', self._canons('Timbre'))

    # ------------------------------------------------------------------ #
    # altLabel matching                                                    #
    # ------------------------------------------------------------------ #

    def test_rock_altlabel_maps_to_rock_music(self) -> None:
        self.assertIn('rock_music', self._canons('rock'))

    def test_pop_altlabel_maps_to_pop_music(self) -> None:
        self.assertIn('pop_music', self._canons('pop'))

    def test_folk_altlabel_maps_to_folk_music(self) -> None:
        self.assertIn('folk_music', self._canons('folk'))

    def test_classical_altlabel_maps_to_classical_music(self) -> None:
        self.assertIn('classical_music', self._canons('classical'))

    def test_country_altlabel_maps_to_country_music(self) -> None:
        self.assertIn('country_music', self._canons('country'))

    def test_rap_altlabel_maps_to_hip_hop(self) -> None:
        self.assertIn('hip_hop', self._canons('rap'))

    def test_metal_altlabel_maps_to_heavy_metal(self) -> None:
        self.assertIn('heavy_metal', self._canons('metal'))

    def test_punk_altlabel_maps_to_punk_rock(self) -> None:
        self.assertIn('punk_rock', self._canons('punk'))

    def test_beat_altlabel_maps_to_rhythm(self) -> None:
        self.assertIn('rhythm', self._canons('beat'))

    # ------------------------------------------------------------------ #
    # Multi-word span matches                                              #
    # ------------------------------------------------------------------ #

    def test_classical_music_span(self) -> None:
        self.assertIn('classical_music', self._canons('Classical Music'))

    def test_rock_music_span(self) -> None:
        self.assertIn('rock_music', self._canons('Rock Music'))

    def test_pop_music_span(self) -> None:
        self.assertIn('pop_music', self._canons('Pop Music'))

    def test_folk_music_span(self) -> None:
        self.assertIn('folk_music', self._canons('Folk Music'))

    def test_heavy_metal_span(self) -> None:
        self.assertIn('heavy_metal', self._canons('Heavy Metal'))

    def test_hip_hop_span(self) -> None:
        self.assertIn('hip_hop', self._canons('Hip Hop'))

    def test_smooth_jazz_span(self) -> None:
        self.assertIn('smooth_jazz', self._canons('Smooth Jazz'))

    def test_country_music_span(self) -> None:
        self.assertIn('country_music', self._canons('Country Music'))

    def test_punk_rock_span(self) -> None:
        self.assertIn('punk_rock', self._canons('Punk Rock'))

    def test_electronic_music_span(self) -> None:
        self.assertIn('electronic_music', self._canons('Electronic Music'))

    # ------------------------------------------------------------------ #
    # In-sentence span tests                                               #
    # ------------------------------------------------------------------ #

    def test_heavy_metal_in_sentence(self) -> None:
        self.assertIn('heavy_metal', self._canons('I love Heavy Metal music'))

    def test_smooth_jazz_in_sentence(self) -> None:
        self.assertIn('smooth_jazz', self._canons('Playing some Smooth Jazz tonight'))


if __name__ == '__main__':
    unittest.main()
