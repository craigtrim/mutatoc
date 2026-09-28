#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Issue: https://github.com/craigtrim/mutato/issues/5
# Docs: docs/architecture.md
# Validates direct skill detection from econ-20160218.owl (MIXED schema).
# Skills are owl:NamedIndividual leaves - all multi-word (2-4 tokens).
# Tests exact phrase matching, case invariance, and negative cases.

import os
import unittest

from mutato.mda import UniversalMDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'econ-20160218'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/skills#'


class TestEconSkillsBasic(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = UniversalMDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='econ')
        cls.api = MutatoAPI(find_ontology_data=cls.finder)

    def _swaps(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['swaps'] for t in result if t.get('swaps')]

    def _canons(self, text: str) -> list:
        return [s['canon'] for s in self._swaps(text)]

    def _normals(self, text: str) -> list:
        result = self.api.swap_input_text(text)
        if not result:
            return []
        return [t['normal'] for t in result if 'normal' in t]

    def _has_span_swap(self, text: str) -> bool:
        return any(s['type'] == 'span' for s in self._swaps(text))

    # ------------------------------------------------------------------ #
    # Result shape                                                         #
    # ------------------------------------------------------------------ #

    def test_result_is_a_list(self) -> None:
        result = self.api.swap_input_text('Fiscal Policy Analysis')
        self.assertIsInstance(result, list)

    def test_each_token_is_a_dict(self) -> None:
        result = self.api.swap_input_text('Fiscal Policy Analysis')
        for token in result:
            self.assertIsInstance(token, dict)

    def test_all_tokens_have_normal_field(self) -> None:
        result = self.api.swap_input_text('Fiscal Policy Analysis')
        for token in result:
            self.assertIn('normal', token)

    def test_normal_field_is_lowercase(self) -> None:
        result = self.api.swap_input_text('FISCAL POLICY ANALYSIS')
        for token in result:
            self.assertEqual(token['normal'], token['normal'].lower())

    def test_none_input_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(None))

    def test_empty_string_returns_none(self) -> None:
        self.assertIsNone(self.api.swap_input_text(''))

    # ------------------------------------------------------------------ #
    # 3-word skills  - direct exact match                                  #
    # ------------------------------------------------------------------ #

    def test_fiscal_policy_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Fiscal Policy Analysis')), 0)

    def test_fiscal_policy_analysis_canon(self) -> None:
        self.assertIn('fiscal_policy_analysis', self._canons('Fiscal Policy Analysis'))

    def test_monetary_policy_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Monetary Policy Analysis')), 0)

    def test_monetary_policy_analysis_canon(self) -> None:
        self.assertIn('monetary_policy_analysis', self._canons('Monetary Policy Analysis'))

    def test_labor_market_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Labor Market Analysis')), 0)

    def test_labor_market_analysis_canon(self) -> None:
        self.assertIn('labor_market_analysis', self._canons('Labor Market Analysis'))

    def test_unemployment_data_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Unemployment Data Analysis')), 0)

    def test_unemployment_data_analysis_canon(self) -> None:
        self.assertIn('unemployment_data_analysis', self._canons('Unemployment Data Analysis'))

    def test_cost_benefit_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Cost Benefit Analysis')), 0)

    def test_cost_benefit_analysis_canon(self) -> None:
        self.assertIn('cost_benefit_analysis', self._canons('Cost Benefit Analysis'))

    def test_market_failure_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Market Failure Analysis')), 0)

    def test_market_failure_analysis_canon(self) -> None:
        self.assertIn('market_failure_analysis', self._canons('Market Failure Analysis'))

    def test_business_cycle_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Business Cycle Analysis')), 0)

    def test_business_cycle_analysis_canon(self) -> None:
        self.assertIn('business_cycle_analysis', self._canons('Business Cycle Analysis'))

    def test_interest_rate_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Interest Rate Analysis')), 0)

    def test_interest_rate_analysis_canon(self) -> None:
        self.assertIn('interest_rate_analysis', self._canons('Interest Rate Analysis'))

    def test_income_inequality_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Income Inequality Analysis')), 0)

    def test_income_inequality_analysis_canon(self) -> None:
        self.assertIn('income_inequality_analysis', self._canons('Income Inequality Analysis'))

    def test_international_trade_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('International Trade Analysis')), 0)

    def test_international_trade_analysis_canon(self) -> None:
        self.assertIn('international_trade_analysis', self._canons('International Trade Analysis'))

    def test_economic_policy_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Economic Policy Analysis')), 0)

    def test_economic_policy_analysis_canon(self) -> None:
        self.assertIn('economic_policy_analysis', self._canons('Economic Policy Analysis'))

    def test_public_policy_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Public Policy Analysis')), 0)

    def test_public_policy_analysis_canon(self) -> None:
        self.assertIn('public_policy_analysis', self._canons('Public Policy Analysis'))

    def test_price_elasticity_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Price Elasticity Analysis')), 0)

    def test_price_elasticity_analysis_canon(self) -> None:
        self.assertIn('price_elasticity_analysis', self._canons('Price Elasticity Analysis'))

    def test_consumer_behavior_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Consumer Behavior Analysis')), 0)

    def test_consumer_behavior_analysis_canon(self) -> None:
        self.assertIn('consumer_behavior_analysis', self._canons('Consumer Behavior Analysis'))

    def test_market_structure_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Market Structure Analysis')), 0)

    def test_market_structure_analysis_canon(self) -> None:
        self.assertIn('market_structure_analysis', self._canons('Market Structure Analysis'))

    # ------------------------------------------------------------------ #
    # 4-word skill  - direct exact match                                   #
    # ------------------------------------------------------------------ #

    def test_supply_and_demand_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Supply And Demand Analysis')), 0)

    def test_supply_and_demand_analysis_canon(self) -> None:
        self.assertIn('supply_and_demand_analysis', self._canons('Supply And Demand Analysis'))

    def test_aggregate_demand_analysis_produces_swap(self) -> None:
        self.assertGreater(len(self._swaps('Aggregate Demand Analysis')), 0)

    def test_aggregate_demand_analysis_canon(self) -> None:
        self.assertIn('aggregate_demand_analysis', self._canons('Aggregate Demand Analysis'))

    # ------------------------------------------------------------------ #
    # Case invariance                                                      #
    # ------------------------------------------------------------------ #

    def test_fiscal_lowercase(self) -> None:
        self.assertIn('fiscal_policy_analysis', self._canons('fiscal policy analysis'))

    def test_fiscal_uppercase(self) -> None:
        self.assertIn('fiscal_policy_analysis', self._canons('FISCAL POLICY ANALYSIS'))

    def test_monetary_mixed_case(self) -> None:
        self.assertIn('monetary_policy_analysis', self._canons('Monetary POLICY analysis'))

    def test_labor_lowercase(self) -> None:
        self.assertIn('labor_market_analysis', self._canons('labor market analysis'))

    # ------------------------------------------------------------------ #
    # Multi-skill input  - both skills detected                            #
    # ------------------------------------------------------------------ #

    def test_two_skills_both_detected(self) -> None:
        canons = self._canons('Fiscal Policy Analysis and Monetary Policy Analysis')
        self.assertIn('fiscal_policy_analysis', canons)
        self.assertIn('monetary_policy_analysis', canons)

    def test_three_skills_all_detected(self) -> None:
        canons = self._canons('Labor Market Analysis Fiscal Policy Analysis Interest Rate Analysis')
        self.assertIn('labor_market_analysis', canons)
        self.assertIn('fiscal_policy_analysis', canons)
        self.assertIn('interest_rate_analysis', canons)

    # ------------------------------------------------------------------ #
    # Negative cases                                                       #
    # ------------------------------------------------------------------ #

    def test_unknown_word_returns_list(self) -> None:
        result = self.api.swap_input_text('zygomorphic')
        self.assertIsInstance(result, list)

    def test_unknown_word_has_no_swaps(self) -> None:
        self.assertEqual(len(self._swaps('zygomorphic')), 0)

    def test_partial_skill_name_no_swap(self) -> None:
        # "Policy" alone is not a skill
        swaps = self._swaps('Policy')
        self.assertEqual(len(swaps), 0)

    def test_nonsense_words_no_swap(self) -> None:
        # Gibberish words that share no tokens with any skill entity
        canons = self._canons('flibbertigibbet wombozzle quuxian')
        self.assertEqual(len(canons), 0)


if __name__ == '__main__':
    unittest.main()
