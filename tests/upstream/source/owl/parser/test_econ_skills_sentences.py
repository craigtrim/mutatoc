#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Issue: https://github.com/craigtrim/mutato/issues/5
# Docs: docs/architecture.md
# Validates skill detection from realistic prose sentences using econ-20160218.owl.
# Each test presents a sentence a hiring manager or resume scanner would encounter
# and asserts the expected skill(s) are recognised.

import os
import unittest

from mutato.mda import UniversalMDAGenerator
from mutato.finder.multiquery import FindOntologyJSON
from mutato.parser import MutatoAPI

os.environ['SPAN_DISTANCE'] = '4'

ONTOLOGY_NAME = 'econ-20160218'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://graffl.ai/skills#'


class TestEconSkillsSentences(unittest.TestCase):

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

    def _has_any_swap(self, text: str) -> bool:
        return len(self._swaps(text)) > 0

    # ------------------------------------------------------------------ #
    # Single skill per sentence                                            #
    # ------------------------------------------------------------------ #

    def test_sentence_fiscal_policy_analysis(self) -> None:
        canons = self._canons('The candidate has extensive expertise in fiscal policy analysis')
        self.assertIn('fiscal_policy_analysis', canons)

    def test_sentence_monetary_policy_analysis(self) -> None:
        canons = self._canons('She specialises in monetary policy analysis at central banks')
        self.assertIn('monetary_policy_analysis', canons)

    def test_sentence_labor_market_analysis(self) -> None:
        canons = self._canons('His research focuses on labor market analysis and wages')
        self.assertIn('labor_market_analysis', canons)

    def test_sentence_unemployment_data_analysis(self) -> None:
        canons = self._canons('Proficient in unemployment data analysis using statistical methods')
        self.assertIn('unemployment_data_analysis', canons)

    def test_sentence_cost_benefit_analysis(self) -> None:
        canons = self._canons('Applied cost benefit analysis to infrastructure investment decisions')
        self.assertIn('cost_benefit_analysis', canons)

    def test_sentence_market_failure_analysis(self) -> None:
        canons = self._canons('Conducted market failure analysis to identify policy interventions')
        self.assertIn('market_failure_analysis', canons)

    def test_sentence_business_cycle_analysis(self) -> None:
        canons = self._canons('Strong background in business cycle analysis and macroeconomics')
        self.assertIn('business_cycle_analysis', canons)

    def test_sentence_interest_rate_analysis(self) -> None:
        canons = self._canons('Performed interest rate analysis for bond portfolio management')
        self.assertIn('interest_rate_analysis', canons)

    def test_sentence_income_inequality_analysis(self) -> None:
        canons = self._canons('Published research on income inequality analysis in emerging markets')
        self.assertIn('income_inequality_analysis', canons)

    def test_sentence_international_trade_analysis(self) -> None:
        canons = self._canons('Led international trade analysis projects for the IMF')
        self.assertIn('international_trade_analysis', canons)

    def test_sentence_supply_and_demand_analysis(self) -> None:
        canons = self._canons('Core skills include supply and demand analysis in commodity markets')
        self.assertIn('supply_and_demand_analysis', canons)

    def test_sentence_economic_policy_analysis(self) -> None:
        canons = self._canons('Delivered economic policy analysis to government advisory boards')
        self.assertIn('economic_policy_analysis', canons)

    def test_sentence_public_policy_analysis(self) -> None:
        canons = self._canons('Expert in public policy analysis and programme evaluation')
        self.assertIn('public_policy_analysis', canons)

    def test_sentence_price_elasticity_analysis(self) -> None:
        canons = self._canons('Applied price elasticity analysis to consumer goods markets')
        self.assertIn('price_elasticity_analysis', canons)

    def test_sentence_consumer_behavior_analysis(self) -> None:
        canons = self._canons('Research includes consumer behavior analysis and demand modelling')
        self.assertIn('consumer_behavior_analysis', canons)

    def test_sentence_market_structure_analysis(self) -> None:
        canons = self._canons('Wrote reports on market structure analysis for antitrust regulators')
        self.assertIn('market_structure_analysis', canons)

    def test_sentence_aggregate_demand_analysis(self) -> None:
        canons = self._canons('Published papers on aggregate demand analysis during recessions')
        self.assertIn('aggregate_demand_analysis', canons)

    def test_sentence_economic_growth_forecasting(self) -> None:
        canons = self._canons('Developed models for economic growth forecasting in developing nations')
        self.assertIn('economic_growth_forecasting', canons)

    def test_sentence_monetary_policy_implementation(self) -> None:
        canons = self._canons('Advised central banks on monetary policy implementation frameworks')
        self.assertIn('monetary_policy_implementation', canons)

    def test_sentence_fiscal_policy_development(self) -> None:
        canons = self._canons('Contributed to fiscal policy development for the treasury department')
        self.assertIn('fiscal_policy_development', canons)

    # ------------------------------------------------------------------ #
    # Multiple skills per sentence                                         #
    # ------------------------------------------------------------------ #

    def test_sentence_fiscal_and_monetary(self) -> None:
        canons = self._canons(
            'Experience in fiscal policy analysis and monetary policy analysis'
        )
        self.assertIn('fiscal_policy_analysis', canons)
        self.assertIn('monetary_policy_analysis', canons)

    def test_sentence_labor_and_unemployment(self) -> None:
        canons = self._canons(
            'Skills in labor market analysis and unemployment data analysis'
        )
        self.assertIn('labor_market_analysis', canons)
        self.assertIn('unemployment_data_analysis', canons)

    def test_sentence_cost_and_market_failure(self) -> None:
        canons = self._canons(
            'Applied cost benefit analysis to address market failure analysis challenges'
        )
        self.assertIn('cost_benefit_analysis', canons)
        self.assertIn('market_failure_analysis', canons)

    def test_sentence_three_skills(self) -> None:
        canons = self._canons(
            'Expertise spans fiscal policy analysis, labor market analysis, '
            'and interest rate analysis'
        )
        self.assertIn('fiscal_policy_analysis', canons)
        self.assertIn('labor_market_analysis', canons)
        self.assertIn('interest_rate_analysis', canons)

    def test_sentence_macro_skill_combination(self) -> None:
        canons = self._canons(
            'Background includes business cycle analysis, economic policy analysis, '
            'and monetary policy analysis'
        )
        self.assertIn('business_cycle_analysis', canons)
        self.assertIn('economic_policy_analysis', canons)
        self.assertIn('monetary_policy_analysis', canons)

    # ------------------------------------------------------------------ #
    # Sentence contains no skill                                           #
    # ------------------------------------------------------------------ #

    def test_sentence_no_skill_no_swaps(self) -> None:
        swaps = self._swaps('The weather in London was particularly grey today')
        self.assertEqual(len(swaps), 0)

    def test_sentence_irrelevant_domain_no_skill_swap(self) -> None:
        # Contains "market" but not a full entity label
        canons = self._canons('The stock market opened higher on Tuesday')
        self.assertNotIn('market_failure_analysis', canons)
        self.assertNotIn('labor_market_analysis', canons)

    # ------------------------------------------------------------------ #
    # Resume-style bullet points                                           #
    # ------------------------------------------------------------------ #

    def test_resume_bullet_fiscal(self) -> None:
        canons = self._canons('Fiscal policy analysis: advised on budget consolidation')
        self.assertIn('fiscal_policy_analysis', canons)

    def test_resume_bullet_unemployment(self) -> None:
        canons = self._canons('Unemployment data analysis  - produced quarterly trend reports')
        self.assertIn('unemployment_data_analysis', canons)

    def test_resume_bullet_supply_demand(self) -> None:
        canons = self._canons('Supply and demand analysis across global commodity sectors')
        self.assertIn('supply_and_demand_analysis', canons)

    def test_resume_bullet_cost_benefit(self) -> None:
        canons = self._canons('Cost benefit analysis for major infrastructure projects')
        self.assertIn('cost_benefit_analysis', canons)

    def test_resume_bullet_international_trade(self) -> None:
        canons = self._canons('International trade analysis for export-led growth strategies')
        self.assertIn('international_trade_analysis', canons)

    # ------------------------------------------------------------------ #
    # Skill appears at start, middle, and end of sentence                 #
    # ------------------------------------------------------------------ #

    def test_skill_at_start_of_sentence(self) -> None:
        canons = self._canons('Labor market analysis is her primary area of expertise')
        self.assertIn('labor_market_analysis', canons)

    def test_skill_in_middle_of_sentence(self) -> None:
        canons = self._canons('Her team uses fiscal policy analysis to guide budget recommendations')
        self.assertIn('fiscal_policy_analysis', canons)

    def test_skill_at_end_of_sentence(self) -> None:
        canons = self._canons('The consultant was hired specifically for interest rate analysis')
        self.assertIn('interest_rate_analysis', canons)


if __name__ == '__main__':
    unittest.main()
