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
    # Purpose:  Clean by-predicate Function
    # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/1059
    # Updated:  13-Oct-2025
    # -----------------------------------------------------------------------------

    def setUp(self) -> None:

        sw = Stopwatch()

        d_owl = MDAGenerator(
            ontology_name='courses-20251013',
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

    def test_by_predicate(self) -> None:

        sw = Stopwatch()

        results: dict[str, list[str]] = self.finder.by_predicate(
            'owl:intersectionOf')
        self.assertIsNotNone(results)

        # exclude 'class'
        self.assertTrue('class' not in list(results.keys()))

        # keep key out of list[str]
        self.assertTrue('coaching' not in results['coaching'])


if __name__ == '__main__':
    unittest.main()
