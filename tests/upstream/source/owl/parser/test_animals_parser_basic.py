#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests basic exact-match and altLabel-match behaviour against the animals-test
# ontology.  Every animal entity is a single word, so these are pure exact-match
# cases; no span logic is exercised here.

import os
import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'animals-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/animals'


class TestAnimalsParserBasic(unittest.TestCase):

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

    # ------------------------------------------------------------------ #
    # Result shape                                                         #
    # ------------------------------------------------------------------ #

    def test_result_is_a_list(self) -> None:
        result = self.api.swap_input_text('Dog')
        self.assertIsInstance(result, list)

    def test_each_token_is_a_dict(self) -> None:
        result = self.api.swap_input_text('Dog')
        for token in result:
            self.assertIsInstance(token, dict)

    def test_all_tokens_have_normal_field(self) -> None:
        result = self.api.swap_input_text('Dog Cat Horse')
        for token in result:
            self.assertIn('normal', token)

    def test_normal_field_is_lowercase(self) -> None:
        result = self.api.swap_input_text('DOG CAT HORSE')
        for token in result:
            self.assertEqual(token['normal'], token['normal'].lower())

    # ------------------------------------------------------------------ #
    # Single-word exact matches                                            #
    # ------------------------------------------------------------------ #

    def test_dog_exact_match(self) -> None:
        self.assertIn('dog', self._canons('Dog'))

    def test_cat_exact_match(self) -> None:
        self.assertIn('cat', self._canons('Cat'))

    def test_horse_exact_match(self) -> None:
        self.assertIn('horse', self._canons('Horse'))

    def test_elephant_exact_match(self) -> None:
        self.assertIn('elephant', self._canons('Elephant'))

    def test_whale_exact_match(self) -> None:
        self.assertIn('whale', self._canons('Whale'))

    def test_lion_exact_match(self) -> None:
        self.assertIn('lion', self._canons('Lion'))

    def test_tiger_exact_match(self) -> None:
        self.assertIn('tiger', self._canons('Tiger'))

    def test_bear_exact_match(self) -> None:
        self.assertIn('bear', self._canons('Bear'))

    def test_eagle_exact_match(self) -> None:
        self.assertIn('eagle', self._canons('Eagle'))

    def test_parrot_exact_match(self) -> None:
        self.assertIn('parrot', self._canons('Parrot'))

    def test_penguin_exact_match(self) -> None:
        self.assertIn('penguin', self._canons('Penguin'))

    def test_snake_exact_match(self) -> None:
        self.assertIn('snake', self._canons('Snake'))

    def test_lizard_exact_match(self) -> None:
        self.assertIn('lizard', self._canons('Lizard'))

    def test_crocodile_exact_match(self) -> None:
        self.assertIn('crocodile', self._canons('Crocodile'))

    def test_turtle_exact_match(self) -> None:
        self.assertIn('turtle', self._canons('Turtle'))

    def test_salmon_exact_match(self) -> None:
        self.assertIn('salmon', self._canons('Salmon'))

    def test_shark_exact_match(self) -> None:
        self.assertIn('shark', self._canons('Shark'))

    def test_bee_exact_match(self) -> None:
        self.assertIn('bee', self._canons('Bee'))

    def test_butterfly_exact_match(self) -> None:
        self.assertIn('butterfly', self._canons('Butterfly'))

    def test_ant_exact_match(self) -> None:
        self.assertIn('ant', self._canons('Ant'))

    def test_poodle_exact_match(self) -> None:
        self.assertIn('poodle', self._canons('Poodle'))

    def test_labrador_exact_match(self) -> None:
        self.assertIn('labrador', self._canons('Labrador'))

    def test_cobra_exact_match(self) -> None:
        self.assertIn('cobra', self._canons('Cobra'))

    def test_iguana_exact_match(self) -> None:
        self.assertIn('iguana', self._canons('Iguana'))

    # ------------------------------------------------------------------ #
    # Case invariance                                                      #
    # ------------------------------------------------------------------ #

    def test_dog_lowercase(self) -> None:
        self.assertIn('dog', self._canons('dog'))

    def test_dog_uppercase(self) -> None:
        self.assertIn('dog', self._canons('DOG'))

    def test_cat_lowercase(self) -> None:
        self.assertIn('cat', self._canons('cat'))

    def test_shark_uppercase(self) -> None:
        self.assertIn('shark', self._canons('SHARK'))

    # ------------------------------------------------------------------ #
    # altLabel / inflection matching                                       #
    # ------------------------------------------------------------------ #

    def test_canine_altlabel_has_swap(self) -> None:
        swaps = self._swaps('canine')
        self.assertGreater(len(swaps), 0, "'canine' should match Dog via altLabel")

    def test_canine_altlabel_canon_is_dog(self) -> None:
        self.assertIn('dog', self._canons('canine'))

    def test_feline_altlabel_has_swap(self) -> None:
        swaps = self._swaps('feline')
        self.assertGreater(len(swaps), 0, "'feline' should match Cat via altLabel")

    def test_feline_altlabel_canon_is_cat(self) -> None:
        self.assertIn('cat', self._canons('feline'))

    def test_equine_altlabel_has_swap(self) -> None:
        swaps = self._swaps('equine')
        self.assertGreater(len(swaps), 0, "'equine' should match Horse via altLabel")

    def test_serpent_altlabel_canon_is_snake(self) -> None:
        self.assertIn('snake', self._canons('serpent'))

    def test_cetacean_altlabel_canon_is_whale(self) -> None:
        self.assertIn('whale', self._canons('cetacean'))

    def test_grizzly_altlabel_canon_is_bear(self) -> None:
        self.assertIn('bear', self._canons('grizzly'))

    def test_macaw_altlabel_canon_is_parrot(self) -> None:
        self.assertIn('parrot', self._canons('macaw'))

    def test_croc_altlabel_canon_is_crocodile(self) -> None:
        self.assertIn('crocodile', self._canons('croc'))

    def test_tortoise_altlabel_canon_is_turtle(self) -> None:
        self.assertIn('turtle', self._canons('tortoise'))

    def test_lab_altlabel_canon_is_labrador(self) -> None:
        self.assertIn('labrador', self._canons('lab'))

    # ------------------------------------------------------------------ #
    # Non-matching tokens                                                  #
    # ------------------------------------------------------------------ #

    def test_unknown_word_returns_list(self) -> None:
        result = self.api.swap_input_text('xylograph')
        self.assertIsInstance(result, list)

    def test_unknown_word_has_no_swaps(self) -> None:
        swaps = self._swaps('xylograph')
        self.assertEqual(len(swaps), 0)

    def test_none_input_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(None))

    def test_empty_string_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(''))

    # ------------------------------------------------------------------ #
    # Mixed input                                                          #
    # ------------------------------------------------------------------ #

    def test_mixed_known_unknown_tokens(self) -> None:
        result = self.api.swap_input_text('Dog foobar Cat')
        normals = [t['normal'] for t in result if 'normal' in t]
        self.assertIn('dog', normals)
        self.assertIn('cat', normals)

    def test_two_known_entities_both_match(self) -> None:
        canons = self._canons('Dog Cat')
        self.assertIn('dog', canons)
        self.assertIn('cat', canons)

    def test_swap_type_is_exact_for_single_word(self) -> None:
        swaps = self._swaps('Dog')
        self.assertTrue(any(s['type'] == 'exact' for s in swaps))

    def test_swap_type_is_a_known_value(self) -> None:
        valid = {'exact', 'span', 'hierarchy'}
        result = self.api.swap_input_text('Dog Cat Whale')
        for token in result:
            if token.get('swaps'):
                self.assertIn(token['swaps']['type'], valid)


if __name__ == '__main__':
    unittest.main()
