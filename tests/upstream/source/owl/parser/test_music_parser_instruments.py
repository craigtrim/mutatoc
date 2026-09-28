#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests exact, altLabel, and span matching for musical instruments against
# the music-test ontology.  Covers all four instrument families (string, wind,
# percussion, keyboard) and their multi-word span variants.

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'music-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/music'


class TestMusicParserInstruments(unittest.TestCase):

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
    # String instruments  - exact match                                     #
    # ------------------------------------------------------------------ #

    def test_guitar_exact_match(self) -> None:
        self.assertIn('guitar', self._canons('Guitar'))

    def test_violin_exact_match(self) -> None:
        self.assertIn('violin', self._canons('Violin'))

    def test_cello_exact_match(self) -> None:
        self.assertIn('cello', self._canons('Cello'))

    def test_harp_exact_match(self) -> None:
        self.assertIn('harp', self._canons('Harp'))

    def test_ukulele_exact_match(self) -> None:
        self.assertIn('ukulele', self._canons('Ukulele'))

    def test_banjo_exact_match(self) -> None:
        self.assertIn('banjo', self._canons('Banjo'))

    def test_mandolin_exact_match(self) -> None:
        self.assertIn('mandolin', self._canons('Mandolin'))

    # ------------------------------------------------------------------ #
    # Wind instruments  - exact match                                       #
    # ------------------------------------------------------------------ #

    def test_flute_exact_match(self) -> None:
        self.assertIn('flute', self._canons('Flute'))

    def test_trumpet_exact_match(self) -> None:
        self.assertIn('trumpet', self._canons('Trumpet'))

    def test_saxophone_exact_match(self) -> None:
        self.assertIn('saxophone', self._canons('Saxophone'))

    def test_clarinet_exact_match(self) -> None:
        self.assertIn('clarinet', self._canons('Clarinet'))

    def test_oboe_exact_match(self) -> None:
        self.assertIn('oboe', self._canons('Oboe'))

    def test_trombone_exact_match(self) -> None:
        self.assertIn('trombone', self._canons('Trombone'))

    def test_tuba_exact_match(self) -> None:
        self.assertIn('tuba', self._canons('Tuba'))

    def test_piccolo_exact_match(self) -> None:
        self.assertIn('piccolo', self._canons('Piccolo'))

    def test_bassoon_exact_match(self) -> None:
        self.assertIn('bassoon', self._canons('Bassoon'))

    # ------------------------------------------------------------------ #
    # Percussion  - exact match                                             #
    # ------------------------------------------------------------------ #

    def test_drum_exact_match(self) -> None:
        self.assertIn('drum', self._canons('Drum'))

    def test_cymbal_exact_match(self) -> None:
        self.assertIn('cymbal', self._canons('Cymbal'))

    def test_xylophone_exact_match(self) -> None:
        self.assertIn('xylophone', self._canons('Xylophone'))

    def test_marimba_exact_match(self) -> None:
        self.assertIn('marimba', self._canons('Marimba'))

    def test_tambourine_exact_match(self) -> None:
        self.assertIn('tambourine', self._canons('Tambourine'))

    def test_timpani_exact_match(self) -> None:
        self.assertIn('timpani', self._canons('Timpani'))

    # ------------------------------------------------------------------ #
    # Keyboard instruments  - exact match                                   #
    # ------------------------------------------------------------------ #

    def test_piano_exact_match(self) -> None:
        self.assertIn('piano', self._canons('Piano'))

    def test_organ_exact_match(self) -> None:
        self.assertIn('organ', self._canons('Organ'))

    def test_synthesizer_exact_match(self) -> None:
        self.assertIn('synthesizer', self._canons('Synthesizer'))

    def test_harpsichord_exact_match(self) -> None:
        self.assertIn('harpsichord', self._canons('Harpsichord'))

    # ------------------------------------------------------------------ #
    # altLabel matching                                                    #
    # ------------------------------------------------------------------ #

    def test_sax_maps_to_saxophone(self) -> None:
        self.assertIn('saxophone', self._canons('sax'))

    def test_fiddle_maps_to_violin(self) -> None:
        self.assertIn('violin', self._canons('fiddle'))

    def test_synth_maps_to_synthesizer(self) -> None:
        self.assertIn('synthesizer', self._canons('synth'))

    def test_axe_maps_to_guitar(self) -> None:
        self.assertIn('guitar', self._canons('axe'))

    # ------------------------------------------------------------------ #
    # Multi-word span matches                                              #
    # ------------------------------------------------------------------ #

    def test_electric_guitar_span(self) -> None:
        self.assertIn('electric_guitar', self._canons('Electric Guitar'))

    def test_acoustic_guitar_span(self) -> None:
        self.assertIn('acoustic_guitar', self._canons('Acoustic Guitar'))

    def test_bass_guitar_span(self) -> None:
        self.assertIn('bass_guitar', self._canons('Bass Guitar'))

    def test_string_instrument_span(self) -> None:
        swaps = self._swaps('String Instrument')
        self.assertGreater(len(swaps), 0)

    def test_wind_instrument_span(self) -> None:
        swaps = self._swaps('Wind Instrument')
        self.assertGreater(len(swaps), 0)

    def test_percussion_instrument_span(self) -> None:
        swaps = self._swaps('Percussion Instrument')
        self.assertGreater(len(swaps), 0)

    def test_keyboard_instrument_span(self) -> None:
        swaps = self._swaps('Keyboard Instrument')
        self.assertGreater(len(swaps), 0)

    def test_grand_piano_span(self) -> None:
        self.assertIn('grand_piano', self._canons('Grand Piano'))

    def test_snare_drum_span(self) -> None:
        self.assertIn('snare_drum', self._canons('Snare Drum'))

    def test_bass_drum_span(self) -> None:
        self.assertIn('bass_drum', self._canons('Bass Drum'))

    def test_french_horn_span(self) -> None:
        self.assertIn('french_horn', self._canons('French Horn'))

    def test_alto_saxophone_span(self) -> None:
        self.assertIn('alto_saxophone', self._canons('Alto Saxophone'))

    def test_tenor_saxophone_span(self) -> None:
        self.assertIn('tenor_saxophone', self._canons('Tenor Saxophone'))

    # ------------------------------------------------------------------ #
    # Embedded in sentence                                                 #
    # ------------------------------------------------------------------ #

    def test_electric_guitar_in_sentence(self) -> None:
        self.assertIn('electric_guitar', self._canons('She played the Electric Guitar'))

    def test_grand_piano_in_sentence(self) -> None:
        self.assertIn('grand_piano', self._canons('The Grand Piano filled the hall'))


if __name__ == '__main__':
    unittest.main()
