#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/mda-precompute.md


import os
import unittest
from json import dump

from mutato.mda import MDAGenerator

class TestAddFunction(unittest.TestCase):

    # it is sufficient that no exceptions are thrown

    def setUp(self) -> None:
        self.mda = MDAGenerator(
            ontology_name='courses-20250122.owl',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/courses'
        )

    def tearDown(self) -> None:
        self.mda = None

    def test_mda_generator(self):
        d_owl = self.mda.generate()

        file_path: str = os.path.join(
            os.getcwd(),
            'tests/test_data/generated/d_courses_test.json'
        )

        with open(file_path, "w") as file:
            dump(d_owl, file, indent=4)


if __name__ == '__main__':
    unittest.main()
