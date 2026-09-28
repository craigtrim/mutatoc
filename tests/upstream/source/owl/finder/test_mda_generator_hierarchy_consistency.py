#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Verifies that children/parents dicts are reflexive and well-formed

import unittest
from mutato.mda import MDAGenerator

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestMDAGeneratorHierarchyConsistency(unittest.TestCase):

    def setUp(self) -> None:
        self.d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        self.children = self.d_owl['children']
        self.parents = self.d_owl['parents']

    def tearDown(self) -> None:
        self.d_owl = None

    def test_children_values_are_non_empty_lists(self) -> None:
        for entity, child_list in self.children.items():
            self.assertIsInstance(child_list, list)
            self.assertGreater(len(child_list), 0, f"{entity!r} has empty children list")

    def test_parents_values_are_non_empty_lists(self) -> None:
        for entity, parent_list in self.parents.items():
            self.assertIsInstance(parent_list, list)
            self.assertGreater(len(parent_list), 0, f"{entity!r} has empty parents list")

    def test_no_entity_is_its_own_child(self) -> None:
        for entity, child_list in self.children.items():
            self.assertNotIn(entity, child_list, f"{entity!r} appears in its own children list")

    def test_no_entity_is_its_own_parent(self) -> None:
        for entity, parent_list in self.parents.items():
            self.assertNotIn(entity, parent_list, f"{entity!r} appears in its own parents list")

    def test_children_and_parents_are_reflexive(self) -> None:
        # If child in children[parent], then parent should be in parents[child]
        for parent, child_list in list(self.children.items())[:20]:  # sample for speed
            for child in child_list:
                if child in self.parents:
                    self.assertIn(
                        parent, self.parents[child],
                        f"children[{parent!r}] has {child!r}, but parents[{child!r}] missing {parent!r}"
                    )

    def test_children_keys_are_all_strings(self) -> None:
        for entity in self.children:
            self.assertIsInstance(entity, str)

    def test_parents_keys_are_all_strings(self) -> None:
        for entity in self.parents:
            self.assertIsInstance(entity, str)


if __name__ == '__main__':
    unittest.main()
