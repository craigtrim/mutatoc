#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import unittest

from mutato.finder.singlequery.svc import GeneratePlusSpans

class TestSpanGeneration(unittest.TestCase):

    def setUp(self) -> None:
        self.generate = GeneratePlusSpans().process

    def tearDown(self) -> None:
        self.generate = None

    def test_generate(self) -> None:

        result = self.generate({
            "research_methods": [
                "research methods"
            ],
            "research_methods_statistics": [
                "research methods statistics"
            ],
            "statistics": [
                "statistics"
            ],
        })

        self.assertEqual(result, {
            'research_methods ': [
                {
                    'content': ['statistics'],
                    'distance': 4,
                    'forward': True,
                    'reverse': True,
                    'canon': 'research_methods_statistics'
                }
            ]
        })


if __name__ == '__main__':
    unittest.main()
