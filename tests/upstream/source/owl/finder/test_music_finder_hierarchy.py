#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests hierarchy traversal on FindOntologyJSON built from music-test.owl.
# Music_Topic → {Instrument, Genre, Concept}
# Instrument → {String_Instrument, Wind_Instrument, Percussion_Instrument,
#                Keyboard_Instrument} → individual instruments.
# Genre → {Jazz, Classical_Music, Rock_Music, Pop_Music, ...} → sub-genres.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'music-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/music'


class TestMusicFinderHierarchy(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='music')

    # ------------------------------------------------------------------ #
    # children()  - Music_Topic root                                       #
    # ------------------------------------------------------------------ #

    def test_music_topic_children_includes_instrument(self) -> None:
        self.assertIn('Instrument', self.finder.children('Music_Topic'))

    def test_music_topic_children_includes_genre(self) -> None:
        self.assertIn('Genre', self.finder.children('Music_Topic'))

    def test_music_topic_children_includes_concept(self) -> None:
        self.assertIn('Concept', self.finder.children('Music_Topic'))

    # ------------------------------------------------------------------ #
    # children()  - Instrument families                                    #
    # ------------------------------------------------------------------ #

    def test_instrument_children_includes_string(self) -> None:
        self.assertIn('String_Instrument', self.finder.children('Instrument'))

    def test_instrument_children_includes_wind(self) -> None:
        self.assertIn('Wind_Instrument', self.finder.children('Instrument'))

    def test_instrument_children_includes_percussion(self) -> None:
        self.assertIn('Percussion_Instrument', self.finder.children('Instrument'))

    def test_instrument_children_includes_keyboard(self) -> None:
        self.assertIn('Keyboard_Instrument', self.finder.children('Instrument'))

    # ------------------------------------------------------------------ #
    # children()  - String instruments                                     #
    # ------------------------------------------------------------------ #

    def test_string_instrument_children_includes_guitar(self) -> None:
        self.assertIn('Guitar', self.finder.children('String_Instrument'))

    def test_string_instrument_children_includes_violin(self) -> None:
        self.assertIn('Violin', self.finder.children('String_Instrument'))

    def test_string_instrument_children_includes_cello(self) -> None:
        self.assertIn('Cello', self.finder.children('String_Instrument'))

    def test_string_instrument_children_includes_harp(self) -> None:
        self.assertIn('Harp', self.finder.children('String_Instrument'))

    def test_guitar_children_includes_electric_guitar(self) -> None:
        self.assertIn('Electric_Guitar', self.finder.children('Guitar'))

    def test_guitar_children_includes_acoustic_guitar(self) -> None:
        self.assertIn('Acoustic_Guitar', self.finder.children('Guitar'))

    def test_guitar_children_includes_bass_guitar(self) -> None:
        self.assertIn('Bass_Guitar', self.finder.children('Guitar'))

    # ------------------------------------------------------------------ #
    # children()  - Wind instruments                                       #
    # ------------------------------------------------------------------ #

    def test_wind_instrument_children_includes_flute(self) -> None:
        self.assertIn('Flute', self.finder.children('Wind_Instrument'))

    def test_wind_instrument_children_includes_trumpet(self) -> None:
        self.assertIn('Trumpet', self.finder.children('Wind_Instrument'))

    def test_wind_instrument_children_includes_saxophone(self) -> None:
        self.assertIn('Saxophone', self.finder.children('Wind_Instrument'))

    def test_saxophone_children_includes_alto_saxophone(self) -> None:
        self.assertIn('Alto_Saxophone', self.finder.children('Saxophone'))

    def test_saxophone_children_includes_tenor_saxophone(self) -> None:
        self.assertIn('Tenor_Saxophone', self.finder.children('Saxophone'))

    # ------------------------------------------------------------------ #
    # children()  - Percussion                                             #
    # ------------------------------------------------------------------ #

    def test_percussion_children_includes_drum(self) -> None:
        self.assertIn('Drum', self.finder.children('Percussion_Instrument'))

    def test_percussion_children_includes_cymbal(self) -> None:
        self.assertIn('Cymbal', self.finder.children('Percussion_Instrument'))

    def test_drum_children_includes_snare_drum(self) -> None:
        self.assertIn('Snare_Drum', self.finder.children('Drum'))

    def test_drum_children_includes_bass_drum(self) -> None:
        self.assertIn('Bass_Drum', self.finder.children('Drum'))

    # ------------------------------------------------------------------ #
    # children()  - Keyboard instruments                                   #
    # ------------------------------------------------------------------ #

    def test_keyboard_children_includes_piano(self) -> None:
        self.assertIn('Piano', self.finder.children('Keyboard_Instrument'))

    def test_keyboard_children_includes_organ(self) -> None:
        self.assertIn('Organ', self.finder.children('Keyboard_Instrument'))

    def test_keyboard_children_includes_synthesizer(self) -> None:
        self.assertIn('Synthesizer', self.finder.children('Keyboard_Instrument'))

    def test_piano_children_includes_grand_piano(self) -> None:
        self.assertIn('Grand_Piano', self.finder.children('Piano'))

    def test_piano_children_includes_upright_piano(self) -> None:
        self.assertIn('Upright_Piano', self.finder.children('Piano'))

    # ------------------------------------------------------------------ #
    # children()  - Genres                                                 #
    # ------------------------------------------------------------------ #

    def test_genre_children_includes_jazz(self) -> None:
        self.assertIn('Jazz', self.finder.children('Genre'))

    def test_genre_children_includes_classical_music(self) -> None:
        self.assertIn('Classical_Music', self.finder.children('Genre'))

    def test_genre_children_includes_rock_music(self) -> None:
        self.assertIn('Rock_Music', self.finder.children('Genre'))

    def test_jazz_children_includes_bebop(self) -> None:
        self.assertIn('Bebop', self.finder.children('Jazz'))

    def test_jazz_children_includes_smooth_jazz(self) -> None:
        self.assertIn('Smooth_Jazz', self.finder.children('Jazz'))

    def test_classical_music_children_includes_baroque(self) -> None:
        self.assertIn('Baroque', self.finder.children('Classical_Music'))

    def test_classical_music_children_includes_romantic(self) -> None:
        self.assertIn('Romantic', self.finder.children('Classical_Music'))

    def test_rock_music_children_includes_heavy_metal(self) -> None:
        self.assertIn('Heavy_Metal', self.finder.children('Rock_Music'))

    def test_rock_music_children_includes_punk_rock(self) -> None:
        self.assertIn('Punk_Rock', self.finder.children('Rock_Music'))

    # ------------------------------------------------------------------ #
    # children()  - Concepts                                               #
    # ------------------------------------------------------------------ #

    def test_concept_children_includes_melody(self) -> None:
        self.assertIn('Melody', self.finder.children('Concept'))

    def test_concept_children_includes_rhythm(self) -> None:
        self.assertIn('Rhythm', self.finder.children('Concept'))

    def test_concept_children_includes_harmony(self) -> None:
        self.assertIn('Harmony', self.finder.children('Concept'))

    # ------------------------------------------------------------------ #
    # parents()                                                           #
    # ------------------------------------------------------------------ #

    def test_guitar_parent_is_string_instrument(self) -> None:
        self.assertIn('String_Instrument', self.finder.parents('Guitar'))

    def test_electric_guitar_parent_is_guitar(self) -> None:
        self.assertIn('Guitar', self.finder.parents('Electric_Guitar'))

    def test_saxophone_parent_is_wind_instrument(self) -> None:
        self.assertIn('Wind_Instrument', self.finder.parents('Saxophone'))

    def test_alto_saxophone_parent_is_saxophone(self) -> None:
        self.assertIn('Saxophone', self.finder.parents('Alto_Saxophone'))

    def test_piano_parent_is_keyboard_instrument(self) -> None:
        self.assertIn('Keyboard_Instrument', self.finder.parents('Piano'))

    def test_grand_piano_parent_is_piano(self) -> None:
        self.assertIn('Piano', self.finder.parents('Grand_Piano'))

    def test_bebop_parent_is_jazz(self) -> None:
        self.assertIn('Jazz', self.finder.parents('Bebop'))

    def test_heavy_metal_parent_is_rock_music(self) -> None:
        self.assertIn('Rock_Music', self.finder.parents('Heavy_Metal'))

    def test_baroque_parent_is_classical_music(self) -> None:
        self.assertIn('Classical_Music', self.finder.parents('Baroque'))

    def test_jazz_parent_is_genre(self) -> None:
        self.assertIn('Genre', self.finder.parents('Jazz'))

    def test_instrument_parent_is_music_topic(self) -> None:
        self.assertIn('Music_Topic', self.finder.parents('Instrument'))

    # ------------------------------------------------------------------ #
    # ancestors()                                                         #
    # ------------------------------------------------------------------ #

    def test_electric_guitar_ancestors_includes_guitar(self) -> None:
        self.assertIn('Guitar', self.finder.ancestors('Electric_Guitar'))

    def test_electric_guitar_ancestors_includes_string_instrument(self) -> None:
        self.assertIn('String_Instrument', self.finder.ancestors('Electric_Guitar'))

    def test_electric_guitar_ancestors_includes_instrument(self) -> None:
        self.assertIn('Instrument', self.finder.ancestors('Electric_Guitar'))

    def test_electric_guitar_ancestors_includes_music_topic(self) -> None:
        self.assertIn('Music_Topic', self.finder.ancestors('Electric_Guitar'))

    def test_alto_saxophone_ancestors_includes_saxophone(self) -> None:
        self.assertIn('Saxophone', self.finder.ancestors('Alto_Saxophone'))

    def test_alto_saxophone_ancestors_includes_wind_instrument(self) -> None:
        self.assertIn('Wind_Instrument', self.finder.ancestors('Alto_Saxophone'))

    def test_bebop_ancestors_includes_jazz(self) -> None:
        self.assertIn('Jazz', self.finder.ancestors('Bebop'))

    def test_bebop_ancestors_includes_genre(self) -> None:
        self.assertIn('Genre', self.finder.ancestors('Bebop'))

    def test_grand_piano_ancestors_includes_piano(self) -> None:
        self.assertIn('Piano', self.finder.ancestors('Grand_Piano'))

    def test_grand_piano_ancestors_includes_keyboard_instrument(self) -> None:
        self.assertIn('Keyboard_Instrument', self.finder.ancestors('Grand_Piano'))

    # ------------------------------------------------------------------ #
    # has_parent / has_ancestor                                           #
    # ------------------------------------------------------------------ #

    def test_has_parent_guitar_string_instrument(self) -> None:
        self.assertTrue(self.finder.has_parent('Guitar', 'String_Instrument'))

    def test_has_parent_negative_guitar_wind(self) -> None:
        self.assertFalse(self.finder.has_parent('Guitar', 'Wind_Instrument'))

    def test_has_ancestor_electric_guitar_music_topic(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Electric_Guitar', 'Music_Topic'))

    def test_has_ancestor_bebop_music_topic(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Bebop', 'Music_Topic'))

    def test_has_ancestor_negative_bebop_instrument(self) -> None:
        self.assertFalse(self.finder.has_ancestor('Bebop', 'Instrument'))

    # ------------------------------------------------------------------ #
    # descendants()                                                       #
    # ------------------------------------------------------------------ #

    def test_guitar_descendants_includes_electric_guitar(self) -> None:
        self.assertIn('Electric_Guitar', self.finder.descendants('Guitar'))

    def test_string_instrument_descendants_includes_electric_guitar(self) -> None:
        self.assertIn('Electric_Guitar', self.finder.descendants('String_Instrument'))

    def test_instrument_descendants_includes_grand_piano(self) -> None:
        self.assertIn('Grand_Piano', self.finder.descendants('Instrument'))

    def test_jazz_descendants_includes_bebop(self) -> None:
        self.assertIn('Bebop', self.finder.descendants('Jazz'))

    def test_music_topic_descendants_includes_bebop(self) -> None:
        self.assertIn('Bebop', self.finder.descendants('Music_Topic'))


if __name__ == '__main__':
    unittest.main()
