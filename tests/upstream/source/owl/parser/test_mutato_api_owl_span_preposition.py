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

    # https://github.com/Maryville-University-DLX/transcriptiq/issues/351

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

    def test_swaps_01(self) -> None:
        swaps = self.api.swap_input_text("Research Methods in Statistics")
        entities: list[str] = [
            x['normal'] for x in swaps if 'normal' in x
        ]

        print(entities)
        self.assertEqual(entities, ['research_methods_statistics'])
        # self.assertEqual(entities, ['research_methods', 'in', 'statistics'])


if __name__ == '__main__':
    unittest.main()
