#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import os
import unittest

import spacy
from spacy.lang.en import English
from mutato.mda import MDAGenerator
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPI(unittest.TestCase):

    # https://github.com/Maryville-University-DLX/transcriptiq/issues/419

    def setUp(self) -> None:
        d_owl = MDAGenerator(
            ontology_name='courses-20241129',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        ).generate()

        self.finder = FindOntologyJSON(
            d_owl=d_owl, ontology_name='courses')

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

        swaps = self.api.swap_input_text("JV ADV WOMENS CHOIR 2021 A")

        entities: list[str] = [
            x['normal'] for x in swaps if 'normal' in x
        ]

        self.assertTrue('womens_choir' in entities)


    def test_swap_input_text_02(self) -> None:

        swaps = self.api.swap_input_text("womens choir")

        entities: list[str] = [
            x['normal'] for x in swaps if 'normal' in x
        ]

        self.assertTrue('womens_choir' in entities)

    def test_swap_input_text_03(self) -> None:

        for input_text in ["women's choir", "womens choir"]:

            swaps = self.api.swap_input_text(input_text)

            entities: list[str] = [
                x['normal'] for x in swaps if 'normal' in x
            ]

            self.assertTrue('womens_choir' in entities)


if __name__ == '__main__':
    unittest.main()
