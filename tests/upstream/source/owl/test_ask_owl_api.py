#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/architecture.md


import unittest

from mutato.core import Enforcer
from mutato.finder.singlequery import AskOwlAPI

class TestAddFunction(unittest.TestCase):

    def setUp(self) -> None:
        self.api = AskOwlAPI(
            ontology_name='medicopilot',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        )

    def tearDown(self) -> None:
        self.api = None

    def test_keyed_labels(self):
        d_labels = self.api.keyed_labels()
        self.assertTrue(isinstance(d_labels, dict))

        for entity in d_labels:
            self.assertTrue(isinstance(entity, str))
            self.assertTrue(isinstance(d_labels[entity], str))

    def test_predicates(self):
        self.assertIsNotNone(self.api.predicates())

    def test_entities(self):
        entities: list[str] = self.api.entities()
        Enforcer.is_list_of_str(entities)

    def test_children(self):
        self.assertEqual(self.api.children('Gluconate'), [
            'Calcium_Gluconate', 'Copper_Gluconate',
            'Iron_Gluconate', 'Magnesium_Gluconate',
            'Potassium_Gluconate', 'Zinc_Gluconate'
        ])

        for entity in self.api.entities():
            children = self.api.children(entity)
            if children:
                Enforcer.is_list_of_str(children)

    def test_descendants(self):
        self.assertEqual(self.api.descendants('Gluconate'), [
            'Calcium_Gluconate', 'CalciumGluconateInjection', 'CalciumGluconateSupplement',
            'IOCalciumGluconateAdministration', 'IVCalciumGluconateAdministration', 'IVCalciumGluconateInfusion',
            'IntravenousCalciumGluconateInfusion', 'OralCalciumGluconateSupplement',
            'SubcutaneousCalciumGluconateInjection', 'TopicalCalciumGluconateGel', 'Copper_Gluconate',
            'Iron_Gluconate', 'Magnesium_Gluconate', 'Potassium_Gluconate', 'Zinc_Gluconate'
        ])

        for entity in self.api.entities():
            descendants = self.api.descendants(entity)
            if descendants:
                Enforcer.is_list_of_str(descendants)

    def test_parents(self):
        self.assertEqual(self.api.parents('Copper_Gluconate'), ['Gluconate'])

        for entity in self.api.entities():
            parents = self.api.parents(entity)
            if parents:
                Enforcer.is_list_of_str(parents)

    def test_ancestors(self):
        self.assertEqual(
            self.api.ancestors('Copper_Gluconate'), [
                'Treatment', 'Medication', 'Gluconate', 'Event'
            ])

        for entity in self.api.entities():
            ancestors = self.api.ancestors(entity)
            if ancestors:
                Enforcer.is_list_of_str(ancestors)


if __name__ == '__main__':
    unittest.main()
