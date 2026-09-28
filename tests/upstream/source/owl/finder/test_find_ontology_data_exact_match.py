#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import unittest

from mutato import MutatoAPI
from mutato.finder.multiquery import FindOntologyData

class TestFindOntologyJSON(unittest.TestCase):

    # -----------------------------------------------------------------------------
    # Purpose:  Test Positive Case in Defect
    # Defect:   https://github.com/Maryville-University-DLX/transcriptiq/issues/513
    # Updated:  22-Jan-2025
    # -----------------------------------------------------------------------------

    def setUp(self) -> None:

        finder = FindOntologyData(
            ontologies=['courses-20250122'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
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
