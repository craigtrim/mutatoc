#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import os
import unittest

import spacy
from spacy.lang.en import English
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyData

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPI(unittest.TestCase):

    # Demonstrate the efficacy of bigram spanning
    # https://github.com/Maryville-University-DLX/transcriptiq/issues/352#issuecomment-2436400799

    def setUp(self) -> None:
        self.finder = FindOntologyData(
            ontologies=['courses-20241023'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        )

        assert self.finder

        # -----------------------------------------------------------------------------
        # Purpose:  Test Pre-Load of spaCy Model
        # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/197
        # Updated:  16-Aug-2024
        # -----------------------------------------------------------------------------
        en_spacy_model: English = spacy.load('en_core_web_sm')
        # -----------------------------------------------------------------------------

        self.api = MutatoAPI(
            find_ontology_data=self.finder,
            en_spacy_model=en_spacy_model  # tiq-197
        )

        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swap_input_text_01(self) -> None:

        for input_text in [
            'Research Methods',
            'Research blah Methods',
            'Research blah blah Methods',
            'Research blah blah blah Methods',
        ]:

            swaps = self.api.swap_input_text(input_text)

            entities: list[str] = [
                x['normal'] for x in swaps if 'normal' in x
            ]

            print(entities)

            self.assertEqual(entities, ['research_methods'])

    def test_swap_input_text_02(self) -> None:

        for input_text in [
            'Research blah blah blah blah Methods',
        ]:

            swaps = self.api.swap_input_text(input_text)

            entities: list[str] = [
                x['normal'] for x in swaps if 'normal' in x
            ]

            print(entities)

            self.assertEqual(entities, [
                'research', 'blah', 'blah', 'blah', 'blah', 'methods'
            ])


if __name__ == '__main__':
    unittest.main()
