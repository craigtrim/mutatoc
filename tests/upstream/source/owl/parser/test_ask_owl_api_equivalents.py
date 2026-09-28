#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import os
import unittest

from mutato.core import Stopwatch
from mutato.finder.singlequery import AskOwlAPI

os.environ['SPAN_DISTANCE'] = '4'


class TestMutatoAPI(unittest.TestCase):

    # -----------------------------------------------------------------------------
    # Purpose:  Clean by-predicate Function
    # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/1062
    # Updated:  14-Oct-2025
    # -----------------------------------------------------------------------------

    def setUp(self) -> None:

        sw = Stopwatch()

        self.api = AskOwlAPI(
            ontology_name='courses-20251013',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        )

        print(f"Ontology File Loaded: {str(sw)}")
        assert self.api

    def tearDown(self) -> None:
        return super().tearDown()

    def test_by_predicate(self) -> None:
        equivalents: dict[str, list[str]] = self.api.equivalents()
        for k in equivalents:
            self.assertFalse(' ' in equivalents[k])
            for v in equivalents[k]:
                self.assertFalse(' ' in v)


if __name__ == '__main__':
    unittest.main()
