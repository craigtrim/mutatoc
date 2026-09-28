#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# -----------------------------------------------------------------------------
# Purpose:  Demonstrate OWL-to-JSON round-trip via filesystem
# Issue:    https://github.com/craigtrim/mutato/issues/1
# -----------------------------------------------------------------------------


import os
import unittest

import spacy
from spacy.lang.en import English

from mutato.core import FileIO, Stopwatch
from mutato.mda import MDAGenerator
from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyData, FindOntologyJSON

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'medic-copilot-20230726'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'
TMP_PATH = f'tests/test_data/generated/{ONTOLOGY_NAME}-roundtrip.json'
INPUT_TEXT = 'aws data ai'


class TestRoundTrip(unittest.TestCase):
    # -----------------------------------------------------------------------------
    # Purpose:  Demonstrate OWL-to-JSON round-trip via filesystem
    # Issue:    https://github.com/craigtrim/mutato/issues/1
    # -----------------------------------------------------------------------------

    def setUp(self) -> None:

        sw = Stopwatch()

        en_spacy_model: English = spacy.load('en_core_web_sm')

        # OWL path: parse directly from the OWL file via SPARQL
        owl_finder = FindOntologyData(
            ontologies=[ONTOLOGY_NAME],
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        )
        self.owl_api = MutatoAPI(
            find_ontology_data=owl_finder,
            en_spacy_model=en_spacy_model
        )

        # Materialize OWL → dict, write to /tmp (simulating upload to S3)
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        FileIO.write_json(d_owl, TMP_PATH)

        # Read back from /tmp (simulating Lambda download from S3)
        d_loaded = FileIO.read_json(TMP_PATH)

        # JSON path: parse from the materialized dict
        json_finder = FindOntologyJSON(
            d_owl=d_loaded,
            ontology_name=ONTOLOGY_NAME
        )
        self.json_api = MutatoAPI(
            find_ontology_data=json_finder,
            en_spacy_model=en_spacy_model
        )

        print(f'Round-trip setup completed: {str(sw)}')

    def tearDown(self) -> None:
        return super().tearDown()

    def test_roundtrip(self) -> None:

        owl_result = self.owl_api.swap_input_text(input_text=INPUT_TEXT, ctr=100)
        json_result = self.json_api.swap_input_text(input_text=INPUT_TEXT, ctr=100)

        self.assertIsNotNone(owl_result)
        self.assertIsNotNone(json_result)
        self.assertEqual(owl_result, json_result)


if __name__ == '__main__':
    unittest.main()
