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
            ontologies=['courses-20251028'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        )

        self.api = MutatoAPI(self.finder)
        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swap_input_text_01(self) -> None:
        swaps = self.api.swap_input_text("Gender and Society")

        entities: list[str] = [
            x['normal'] for x in swaps if 'normal' in x
        ]

        self.assertTrue('gender_and_society' in entities)


if __name__ == '__main__':
    unittest.main()
