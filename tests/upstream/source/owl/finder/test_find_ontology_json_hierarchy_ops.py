#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests hierarchy traversal methods on FindOntologyJSON using the Gluconate entity,
# whose full ancestry (Medication → Treatment → Event) is well-understood.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'medic-copilot-20230801'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/medicopilot'


class TestFindOntologyJSONHierarchyOps(unittest.TestCase):

    def setUp(self) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE
        ).generate()
        self.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='medicopilot')

    def tearDown(self) -> None:
        self.finder = None

    def test_gluconate_direct_children(self) -> None:
        children = self.finder.children('Gluconate')
        self.assertIsInstance(children, list)
        self.assertIn('Calcium_Gluconate', children)
        self.assertIn('Copper_Gluconate', children)
        self.assertIn('Zinc_Gluconate', children)

    def test_gluconate_direct_parent(self) -> None:
        parents = self.finder.parents('Gluconate')
        self.assertEqual(parents, ['Medication'])

    def test_gluconate_full_ancestry(self) -> None:
        ancestors = self.finder.ancestors('Gluconate')
        self.assertIn('Medication', ancestors)
        self.assertIn('Treatment', ancestors)
        self.assertIn('Event', ancestors)

    def test_ancestors_and_self_includes_entity(self) -> None:
        result = self.finder.ancestors_and_self('Gluconate')
        self.assertIn('Gluconate', result)
        self.assertIn('Medication', result)
        self.assertIn('Event', result)

    def test_children_and_self_includes_entity(self) -> None:
        result = self.finder.children_and_self('Gluconate')
        self.assertIn('Gluconate', result)
        self.assertIn('Calcium_Gluconate', result)

    def test_descendants_includes_deep_children(self) -> None:
        # CalciumGluconateInjection is a child of Calcium_Gluconate
        descendants = self.finder.descendants('Gluconate')
        self.assertIn('Calcium_Gluconate', descendants)
        self.assertIn('CalciumGluconateInjection', descendants)

    def test_has_parent_gluconate_medication(self) -> None:
        self.assertTrue(self.finder.has_parent('Gluconate', 'Medication'))

    def test_has_parent_negative_case(self) -> None:
        # Gluconate is not a direct parent of itself
        self.assertFalse(self.finder.has_parent('Gluconate', 'Gluconate'))

    def test_has_ancestor_event(self) -> None:
        # Event is a transitive ancestor via Medication → Treatment → Event
        self.assertTrue(self.finder.has_ancestor('Gluconate', 'Event'))

    def test_calcium_gluconate_has_ancestor_gluconate(self) -> None:
        # Calcium_Gluconate is a child of Gluconate, so Gluconate is its ancestor
        self.assertTrue(self.finder.has_ancestor('Calcium_Gluconate', 'Gluconate'))


if __name__ == '__main__':
    unittest.main()
