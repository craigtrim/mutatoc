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
            "penetrating_eye_trauma":
                [
                    "eye_trauma+penetrating_wound",
                ]
        })

        print(result)

        assert result == {
            "eye_trauma": [
                {
                    "canon": "penetrating_eye_trauma",
                    "content": [
                        "penetrating_wound"
                    ],
                    "distance": 4,
                    "forward": True,
                    "reverse": True
                }
            ]
        }


if __name__ == '__main__':
    unittest.main()
