#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import unittest
from pprint import pprint

from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyData

class TestMutatoAPI(unittest.TestCase):

    def setUp(self) -> None:
        
        self.finder = FindOntologyData(
            ontologies=['medic-copilot-20230726'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )

        self.assertIsNotNone(self.finder)

        self.api = MutatoAPI(self.finder)
        assert self.api

        return super().setUp()

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swap(self) -> None:

        swaps = self.api.swap_input_text(input_text='aws data ai', ctr=100)
        self.assertIsNotNone(swaps)

        pprint(f'{len(swaps)}')


if __name__ == '__main__':
    unittest.main()
