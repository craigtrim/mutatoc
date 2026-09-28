#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/mda-precompute.md


import unittest

from mutato.core import FileIO
from mutato.mda import MDAGenerator

class TestFindOntologyJSON(unittest.TestCase):

    def setUp(self) -> None:

        self.mda = MDAGenerator(
            ontology_name='medic-copilot-20230801',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )

    def tearDown(self) -> None:
        self.mda = None

    def test_generate(self) -> None:
        d_owl = self.mda.generate()

        self.assertEqual(
            sorted(d_owl.keys()), [
                'by_predicate', 'children', 'equivalents',
                'labels', 'ner', 'ngrams', 'parents', 'predicates',
                'spans', 'synonyms', 'trie'
            ])

        output_path = FileIO.join_cwd(
            'tests/test_data/ontologies/medic-copilot-20230801.json')
        FileIO.write_json(data=d_owl, file_path=output_path, debug=True)


if __name__ == '__main__':
    unittest.main()
