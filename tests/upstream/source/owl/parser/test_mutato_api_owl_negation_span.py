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
            ontologies=['medic-copilot-20230818'],
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )

        assert self.finder

        self.api = MutatoAPI(self.finder)
        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_swaps(self) -> None:

        swaps = self.api.swap_input_text("but there's no sign of head trauma.")

        print([x['normal'] for x in swaps if 'normal' in x])
        assert 'no_head_trauma' in [
            x['normal'] for x in swaps if 'normal' in x]


if __name__ == '__main__':
    unittest.main()
