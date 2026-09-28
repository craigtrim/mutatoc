#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Issue: https://github.com/craigtrim/mutato/issues/5
# Docs: docs/architecture.md
# Validates span matching for econ-20160218.owl skills in noisy text.
#
# Span matching behaviour for 3-word skills ("Fiscal Policy Analysis"):
#   The span is anchored by the FIRST BIGRAM (e.g. "Fiscal Policy").
#   The final token ("Analysis") may appear within SPAN_DISTANCE=4 of
#   the anchor end.
#
# Invariants confirmed by these tests:
#   pass "X Y blah Z"        - last-pair gap 1 - DETECTED
#   pass "X Y blah blah Z"   - last-pair gap 2 - DETECTED
#   fail "X blah Y Z"        - first-pair gap (breaks bigram anchor) - NOT detected
#   fail "X blah Y blah Z"   - both-pair gap - NOT detected

import os
import unittest

from mutato.mda import UniversalMDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'econ-20160218'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/skills#'


class TestEconSkillsSpans(unittest.TestCase):

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

    def _has_span_swap(self, text: str) -> bool:
        return any(s['type'] == 'span' for s in self._swaps(text))

    # ------------------------------------------------------------------ #
    # Fiscal Policy Analysis  - last-pair gaps                             #
    # ------------------------------------------------------------------ #

    def test_fiscal_span_last_gap_1(self) -> None:
        # "Fiscal Policy blah Analysis"  - 1 filler after "Policy"
        self.assertIn('fiscal_policy_analysis', self._canons('Fiscal Policy blah Analysis'))

    def test_fiscal_span_last_gap_2(self) -> None:
        # "Fiscal Policy blah blah Analysis"  - 2 fillers after "Policy"
        self.assertIn('fiscal_policy_analysis', self._canons('Fiscal Policy blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Monetary Policy Analysis  - last-pair gaps                           #
    # ------------------------------------------------------------------ #

    def test_monetary_span_last_gap_1(self) -> None:
        self.assertIn('monetary_policy_analysis', self._canons('Monetary Policy blah Analysis'))

    def test_monetary_span_last_gap_2(self) -> None:
        self.assertIn('monetary_policy_analysis', self._canons('Monetary Policy blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Labor Market Analysis  - last-pair gaps                              #
    # ------------------------------------------------------------------ #

    def test_labor_span_last_gap_1(self) -> None:
        self.assertIn('labor_market_analysis', self._canons('Labor Market blah Analysis'))

    def test_labor_span_last_gap_2(self) -> None:
        self.assertIn('labor_market_analysis', self._canons('Labor Market blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Market Failure Analysis  - last-pair gaps                            #
    # ------------------------------------------------------------------ #

    def test_market_failure_span_last_gap_1(self) -> None:
        self.assertIn('market_failure_analysis', self._canons('Market Failure blah Analysis'))

    def test_market_failure_span_last_gap_2(self) -> None:
        self.assertIn('market_failure_analysis', self._canons('Market Failure blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Business Cycle Analysis  - last-pair gaps                            #
    # ------------------------------------------------------------------ #

    def test_business_cycle_span_last_gap_1(self) -> None:
        self.assertIn('business_cycle_analysis', self._canons('Business Cycle blah Analysis'))

    def test_business_cycle_span_last_gap_2(self) -> None:
        self.assertIn('business_cycle_analysis', self._canons('Business Cycle blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Interest Rate Analysis  - last-pair gaps                             #
    # ------------------------------------------------------------------ #

    def test_interest_rate_span_last_gap_1(self) -> None:
        self.assertIn('interest_rate_analysis', self._canons('Interest Rate blah Analysis'))

    def test_interest_rate_span_last_gap_2(self) -> None:
        self.assertIn('interest_rate_analysis', self._canons('Interest Rate blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Income Inequality Analysis  - last-pair gaps                         #
    # ------------------------------------------------------------------ #

    def test_income_inequality_span_last_gap_1(self) -> None:
        self.assertIn('income_inequality_analysis', self._canons('Income Inequality blah Analysis'))

    def test_income_inequality_span_last_gap_2(self) -> None:
        self.assertIn('income_inequality_analysis', self._canons('Income Inequality blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Cost Benefit Analysis  - last-pair gaps                              #
    # ------------------------------------------------------------------ #

    def test_cost_benefit_span_last_gap_1(self) -> None:
        self.assertIn('cost_benefit_analysis', self._canons('Cost Benefit blah Analysis'))

    def test_cost_benefit_span_last_gap_2(self) -> None:
        self.assertIn('cost_benefit_analysis', self._canons('Cost Benefit blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # International Trade Analysis  - last-pair gaps                       #
    # ------------------------------------------------------------------ #

    def test_international_trade_span_last_gap_1(self) -> None:
        self.assertIn('international_trade_analysis', self._canons('International Trade blah Analysis'))

    def test_international_trade_span_last_gap_2(self) -> None:
        self.assertIn('international_trade_analysis', self._canons('International Trade blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Price Elasticity Analysis  - last-pair gaps                          #
    # ------------------------------------------------------------------ #

    def test_price_elasticity_span_last_gap_1(self) -> None:
        self.assertIn('price_elasticity_analysis', self._canons('Price Elasticity blah Analysis'))

    def test_price_elasticity_span_last_gap_2(self) -> None:
        self.assertIn('price_elasticity_analysis', self._canons('Price Elasticity blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Consumer Behavior Analysis  - last-pair gaps                         #
    # ------------------------------------------------------------------ #

    def test_consumer_behavior_span_last_gap_1(self) -> None:
        self.assertIn('consumer_behavior_analysis', self._canons('Consumer Behavior blah Analysis'))

    def test_consumer_behavior_span_last_gap_2(self) -> None:
        self.assertIn('consumer_behavior_analysis', self._canons('Consumer Behavior blah blah Analysis'))

    # ------------------------------------------------------------------ #
    # Span distance exceeded  - last-pair gap too large                    #
    # ------------------------------------------------------------------ #

    def test_fiscal_last_gap_exceeded(self) -> None:
        # 5 filler words between "Policy" and "Analysis"  - exceeds SPAN_DISTANCE=4
        text = 'Fiscal Policy blah blah blah blah blah Analysis'
        self.assertNotIn('fiscal_policy_analysis', self._canons(text))

    def test_labor_last_gap_exceeded(self) -> None:
        text = 'Labor Market blah blah blah blah blah Analysis'
        self.assertNotIn('labor_market_analysis', self._canons(text))

    # ------------------------------------------------------------------ #
    # First-pair gap breaks span (bigram anchor invariant)                #
    # ------------------------------------------------------------------ #

    def test_fiscal_first_gap_does_not_produce_full_span(self) -> None:
        # Gap in the first pair destroys the bigram anchor; last 2 tokens match
        # a shorter entity instead, not the full 3-word skill.
        canons = self._canons('Fiscal blah Policy Analysis')
        self.assertNotIn('fiscal_policy_analysis', canons)

    def test_monetary_first_gap_does_not_produce_full_span(self) -> None:
        canons = self._canons('Monetary blah Policy Analysis')
        self.assertNotIn('monetary_policy_analysis', canons)

    def test_labor_first_gap_does_not_produce_full_span(self) -> None:
        canons = self._canons('Labor blah Market Analysis')
        self.assertNotIn('labor_market_analysis', canons)

    # ------------------------------------------------------------------ #
    # Span within a wider sentence                                         #
    # ------------------------------------------------------------------ #

    def test_fiscal_span_in_sentence(self) -> None:
        canons = self._canons('The candidate has experience in Fiscal Policy blah Analysis')
        self.assertIn('fiscal_policy_analysis', canons)

    def test_monetary_span_in_sentence(self) -> None:
        canons = self._canons('Expertise in Monetary Policy blah Analysis and forecasting')
        self.assertIn('monetary_policy_analysis', canons)

    def test_labor_span_in_sentence(self) -> None:
        canons = self._canons('Strong background in Labor Market blah Analysis')
        self.assertIn('labor_market_analysis', canons)

    def test_interest_span_in_sentence(self) -> None:
        canons = self._canons('The bank used Interest Rate blah Analysis to set policy')
        self.assertIn('interest_rate_analysis', canons)


if __name__ == '__main__':
    unittest.main()
