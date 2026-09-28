#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/mda-precompute.md


import unittest

from mutato.core import FileIO
from mutato.finder.singlequery import AskJsonAPI

class TestAddFunction(unittest.TestCase):

    def setUp(self) -> None:

        d_owl = FileIO.read_json(
            'tests/test_data/ontologies/medic-copilot-20230801.json')
        self.api = AskJsonAPI(d_owl=d_owl)

    def tearDown(self) -> None:
        self.api = None

    def test_functions(self):
        for i in range(0, 20):
            self.api.ngrams(i)
        
        self.api.trie()
        self.api.labels()
        # -----------------------------------------------------------------------------
        # Purpose:  Exclude Useless Predicates
        # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/351
        #           issuecomment-2433585518
        # Updated:  23-Oct-2024
        # -----------------------------------------------------------------------------
        # self.api.comments()
        # -----------------------------------------------------------------------------
        self.api.types()
        self.api.predicates()
        self.api.synonyms()
        self.api.synonyms_rev()
        self.api.equivalents()
        self.api.spans()

        for predicate in self.api.predicates():
            self.api.by_predicate(predicate)


if __name__ == '__main__':
    unittest.main()
