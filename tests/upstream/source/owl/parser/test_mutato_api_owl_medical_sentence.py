#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import os
import unittest

from mutato.parser import MutatoAPI
from mutato.finder.multiquery import FindOntologyData

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPI(unittest.TestCase):

    def setUp(self) -> None:
        self.finder = FindOntologyData(
            ontologies=['medic-copilot-20230801'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )

        self.api = MutatoAPI(self.finder)
        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swaps(self) -> None:

        results = self.api.swap_input_text(
            "The reckless behavior caused him a jab leading to an eye injury.")
        self.assertIsNotNone(results)


if __name__ == '__main__':
    unittest.main()
