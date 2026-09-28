#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import os
import unittest

import spacy
from mutato.core import Stopwatch
from spacy.lang.en import English
from mutato.mda import MDAGenerator
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPI(unittest.TestCase):

    # -----------------------------------------------------------------------------
    # Purpose:  Test Punctuation Behavior in Synonyms: `/`, `-`
    # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/528
    #           issuecomment-2542114462
    # Updated:  31-Jan-2025
    # -----------------------------------------------------------------------------

    def setUp(self) -> None:

        sw = Stopwatch()

        d_owl = MDAGenerator(
            ontology_name='courses-20250131',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        ).generate()

        self.finder = FindOntologyJSON(
            d_owl=d_owl, ontology_name='courses')

        en_spacy_model: English = spacy.load('en_core_web_sm')

        self.api = MutatoAPI(
            find_ontology_data=self.finder,
            en_spacy_model=en_spacy_model  # tiq-197
        )

        print(f"Ontology File Loaded: {str(sw)}")
        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swap_input_text_01(self) -> None:

        sw = Stopwatch()

        swaps = self.api.swap_input_text("COMPUTERS-FUTURE OF MAN")

        entities: list[str] = [
            x['normal'] for x in swaps if 'normal' in x
        ]

        print(entities)
        print(f"Entity Swap Completed: {str(sw)}")
        self.assertEqual(entities, ['computers_in_society', 'of', 'man'])


if __name__ == '__main__':
    unittest.main()
