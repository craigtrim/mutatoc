#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Verifies internal consistency between fwd, rev, and lookup synonym structures

import unittest
from mutato.mda import MDAGenerator

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestMDAGeneratorSynonymConsistency(unittest.TestCase):

    def setUp(self) -> None:
        self.d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        self.fwd = self.d_owl['synonyms']['fwd']
        self.rev = self.d_owl['synonyms']['rev']
        self.lookup = self.d_owl['synonyms']['lookup']

    def tearDown(self) -> None:
        self.d_owl = None

    def test_fwd_is_non_empty_dict(self) -> None:
        self.assertIsInstance(self.fwd, dict)
        self.assertGreater(len(self.fwd), 0)

    def test_rev_is_non_empty_dict(self) -> None:
        self.assertIsInstance(self.rev, dict)
        self.assertGreater(len(self.rev), 0)

    def test_lookup_has_integer_keys(self) -> None:
        self.assertIsInstance(self.lookup, dict)
        for key in self.lookup:
            self.assertIsInstance(key, int, f"lookup key is not int: {key!r}")

    def test_fwd_values_are_lists(self) -> None:
        for entity, variants in self.fwd.items():
            self.assertIsInstance(variants, list, f"fwd[{entity!r}] is not a list")

    def test_fwd_variant_lists_contain_strings(self) -> None:
        for entity, variants in self.fwd.items():
            for v in variants:
                self.assertIsInstance(v, str, f"fwd[{entity!r}] contains non-str: {v!r}")

    def test_unigram_lookup_terms_have_no_spaces(self) -> None:
        # n-gram level 1 terms should be single words
        unigrams = self.lookup.get(1, [])
        for term in unigrams:
            self.assertNotIn(' ', term, f"Unigram '{term}' contains a space")

    def test_rev_keys_appear_as_fwd_values(self) -> None:
        # Every key in rev should appear as a value in some fwd entry
        all_fwd_values = {syn for variants in self.fwd.values() for syn in variants}
        for rev_key in list(self.rev.keys())[:30]:  # sample for speed
            self.assertIn(
                rev_key, all_fwd_values,
                f"rev key '{rev_key}' not found in any fwd variant list"
            )


if __name__ == '__main__':
    unittest.main()
