#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/mda-precompute.md


import os
import unittest

from mutato import MutatoAPI
from mutato.finder.multiquery import FindOntologyJSON


class TestFindOntologyJSON(unittest.TestCase):

    # -----------------------------------------------------------------------------
    # Purpose:  Test Negative Case in Defect
    # Defect:   https://github.com/Maryville-University-DLX/transcriptiq/issues/513
    # Updated:  22-Jan-2025
    # -----------------------------------------------------------------------------

    def read_courses_json(self) -> dict:
        from json import load
        with open(os.path.join(os.getcwd(), 'tests/test_data/ontologies/courses-20250122.json'), 'r') as file:
            return load(file)

    def setUp(self) -> None:

        finder = FindOntologyJSON(
            d_owl=self.read_courses_json(),
            ontology_name='courses'
        )

        self.swap_input_text = MutatoAPI(
            find_ontology_data=finder
        ).swap_input_text

    def tearDown(self) -> None:
        self.finder = None

    def test_swap_input_text(self) -> None:
        results = self.swap_input_text('Geometry')

        self.assertEqual(results[0].get('swaps').get('canon'), 'geometry')
        self.assertEqual(results[0].get('swaps').get('type'), 'exact')


if __name__ == '__main__':
    unittest.main()
