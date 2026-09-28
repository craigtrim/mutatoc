#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Docs: docs/mda-precompute.md


import unittest

from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

class TestFindOntologyJSON(unittest.TestCase):

    def setUp(self) -> None:

        d_owl = MDAGenerator(
            ontology_name='medic-copilot-20230801',
            absolute_path='tests/test_data/ontologies',
            namespace='http://graffl.ai/medicopilot'
        ).generate()

        self.finder = FindOntologyJSON(
            d_owl=d_owl, ontology_name='medicopilot')

    def tearDown(self) -> None:
        self.finder = None

    def is_dict_1(self, d: dict) -> None:
        # test for dict[str, list[str]]

        if not d or not len(d):
            return None

        self.assertIsInstance(d, dict)
        for k in d:
            self.assertTrue(isinstance(d[k], list))
            if len(d[k]):
                self.assertTrue(isinstance(d[k][0], str))

    def test_functions(self) -> None:

        predicates: list[str] = [
            ':inflection', ':locatedIn', ':meansOf', ':requires', ':uses'
        ]

        self.assertEqual(self.finder.ontologies(), ['medicopilot'])
        self.assertEqual(sorted(self.finder.predicates()), predicates)

        for predicate in predicates:
            self.is_dict_1(self.finder.by_predicate(predicate))
            self.is_dict_1(self.finder.by_predicate_rev(predicate))

        # -----------------------------------------------------------------------------
        # Purpose:  Exclude Useless Predicates
        # Issue:    https://github.com/Maryville-University-DLX/transcriptiq/issues/351
        #           issuecomment-2433585518
        # Updated:  23-Oct-2024
        # -----------------------------------------------------------------------------
        # self.is_dict_1(self.finder.comments())
        # -----------------------------------------------------------------------------

        d_labels: dict[str, str] = self.finder.labels()
        self.assertTrue(isinstance(d_labels, dict))
        [
            self.assertTrue(isinstance(entity, str)) and
            self.assertTrue(isinstance(d_labels[entity], str))
            for entity in d_labels
        ]

        # empty in this Ontology ...
        self.finder.effects()
        self.finder.effects_rev()
        self.finder.requires()
        self.finder.required_by()

    def test_children(self):
        self.assertEqual(self.finder.children('Gluconate'), [
            'Calcium_Gluconate', 'Copper_Gluconate', 'Iron_Gluconate', 'Magnesium_Gluconate',
            'Potassium_Gluconate', 'Zinc_Gluconate'
        ])

    def test_descendants(self):
        self.assertEqual(self.finder.descendants('Gluconate'), [
            'Calcium_Gluconate', 'CalciumGluconateInjection', 'CalciumGluconateSupplement',
            'IOCalciumGluconateAdministration', 'IVCalciumGluconateAdministration', 'IVCalciumGluconateInfusion',
            'IntravenousCalciumGluconateInfusion', 'OralCalciumGluconateSupplement',
            'SubcutaneousCalciumGluconateInjection', 'TopicalCalciumGluconateGel',
            'Copper_Gluconate', 'Iron_Gluconate', 'Magnesium_Gluconate',
            'Potassium_Gluconate', 'Zinc_Gluconate'
        ])

    def test_descendants_and_self(self):
        self.assertEqual(self.finder.descendants_and_self('Gluconate'), [
            'Gluconate', 'Calcium_Gluconate', 'CalciumGluconateInjection', 'CalciumGluconateSupplement',
            'IOCalciumGluconateAdministration', 'IVCalciumGluconateAdministration', 'IVCalciumGluconateInfusion',
            'IntravenousCalciumGluconateInfusion', 'OralCalciumGluconateSupplement', 'SubcutaneousCalciumGluconateInjection',
            'TopicalCalciumGluconateGel', 'Copper_Gluconate', 'Iron_Gluconate', 'Magnesium_Gluconate',
            'Potassium_Gluconate', 'Zinc_Gluconate'
        ])

    def test_parents(self):
        self.assertEqual(self.finder.parents('Gluconate'), ['Medication'])

    def test_ancestors(self):
        self.assertEqual(
            self.finder.ancestors('Gluconate'), [
                'Medication', 'Treatment', 'Event'
            ])

    def ancestors_and_self(self):
        self.assertEqual(
            self.finder.ancestors_and_self('Gluconate'), [
                'Gluconate', 'Medication', 'Treatment', 'Event'
            ])


if __name__ == '__main__':
    unittest.main()
