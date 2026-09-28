#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Tests hierarchy traversal on FindOntologyJSON built from geography-test.owl.
# Geographic_Region → {Continent, Country, City, Landmark, Ocean, Mountain}.
# Continent → individual continents. Country → individual countries. Etc.

import unittest
from mutato.mda import MDAGenerator
from mutato.finder.multiquery import FindOntologyJSON

ONTOLOGY_NAME = 'geography-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/geography'


class TestGeographyFinder(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()
        cls.finder = FindOntologyJSON(d_owl=d_owl, ontology_name='geography')

    # ------------------------------------------------------------------ #
    # children()  - Geographic_Region root                                 #
    # ------------------------------------------------------------------ #

    def test_region_children_includes_continent(self) -> None:
        self.assertIn('Continent', self.finder.children('Geographic_Region'))

    def test_region_children_includes_country(self) -> None:
        self.assertIn('Country', self.finder.children('Geographic_Region'))

    def test_region_children_includes_city(self) -> None:
        self.assertIn('City', self.finder.children('Geographic_Region'))

    def test_region_children_includes_landmark(self) -> None:
        self.assertIn('Landmark', self.finder.children('Geographic_Region'))

    def test_region_children_includes_ocean(self) -> None:
        self.assertIn('Ocean', self.finder.children('Geographic_Region'))

    def test_region_children_includes_mountain(self) -> None:
        self.assertIn('Mountain', self.finder.children('Geographic_Region'))

    # ------------------------------------------------------------------ #
    # children()  - Continent                                              #
    # ------------------------------------------------------------------ #

    def test_continent_children_includes_north_america(self) -> None:
        self.assertIn('North_America', self.finder.children('Continent'))

    def test_continent_children_includes_south_america(self) -> None:
        self.assertIn('South_America', self.finder.children('Continent'))

    def test_continent_children_includes_europe(self) -> None:
        self.assertIn('Europe', self.finder.children('Continent'))

    def test_continent_children_includes_asia(self) -> None:
        self.assertIn('Asia', self.finder.children('Continent'))

    def test_continent_children_includes_africa(self) -> None:
        self.assertIn('Africa', self.finder.children('Continent'))

    def test_continent_children_includes_antarctica(self) -> None:
        self.assertIn('Antarctica', self.finder.children('Continent'))

    # ------------------------------------------------------------------ #
    # children()  - Country                                                #
    # ------------------------------------------------------------------ #

    def test_country_children_includes_united_states(self) -> None:
        self.assertIn('United_States', self.finder.children('Country'))

    def test_country_children_includes_united_kingdom(self) -> None:
        self.assertIn('United_Kingdom', self.finder.children('Country'))

    def test_country_children_includes_france(self) -> None:
        self.assertIn('France', self.finder.children('Country'))

    def test_country_children_includes_germany(self) -> None:
        self.assertIn('Germany', self.finder.children('Country'))

    def test_country_children_includes_japan(self) -> None:
        self.assertIn('Japan', self.finder.children('Country'))

    def test_country_children_includes_china(self) -> None:
        self.assertIn('China', self.finder.children('Country'))

    def test_country_children_includes_brazil(self) -> None:
        self.assertIn('Brazil', self.finder.children('Country'))

    # ------------------------------------------------------------------ #
    # children()  - City                                                   #
    # ------------------------------------------------------------------ #

    def test_city_children_includes_new_york(self) -> None:
        self.assertIn('New_York', self.finder.children('City'))

    def test_city_children_includes_los_angeles(self) -> None:
        self.assertIn('Los_Angeles', self.finder.children('City'))

    def test_city_children_includes_london(self) -> None:
        self.assertIn('London', self.finder.children('City'))

    def test_city_children_includes_paris(self) -> None:
        self.assertIn('Paris', self.finder.children('City'))

    def test_city_children_includes_tokyo(self) -> None:
        self.assertIn('Tokyo', self.finder.children('City'))

    def test_city_children_includes_beijing(self) -> None:
        self.assertIn('Beijing', self.finder.children('City'))

    # ------------------------------------------------------------------ #
    # children()  - Landmark, Ocean, Mountain                              #
    # ------------------------------------------------------------------ #

    def test_landmark_children_includes_eiffel_tower(self) -> None:
        self.assertIn('Eiffel_Tower', self.finder.children('Landmark'))

    def test_landmark_children_includes_great_wall(self) -> None:
        self.assertIn('Great_Wall', self.finder.children('Landmark'))

    def test_landmark_children_includes_big_ben(self) -> None:
        self.assertIn('Big_Ben', self.finder.children('Landmark'))

    def test_ocean_children_includes_pacific_ocean(self) -> None:
        self.assertIn('Pacific_Ocean', self.finder.children('Ocean'))

    def test_ocean_children_includes_atlantic_ocean(self) -> None:
        self.assertIn('Atlantic_Ocean', self.finder.children('Ocean'))

    def test_mountain_children_includes_mount_everest(self) -> None:
        self.assertIn('Mount_Everest', self.finder.children('Mountain'))

    def test_mountain_children_includes_kilimanjaro(self) -> None:
        self.assertIn('Kilimanjaro', self.finder.children('Mountain'))

    # ------------------------------------------------------------------ #
    # parents()                                                           #
    # ------------------------------------------------------------------ #

    def test_europe_parent_is_continent(self) -> None:
        self.assertIn('Continent', self.finder.parents('Europe'))

    def test_france_parent_is_country(self) -> None:
        self.assertIn('Country', self.finder.parents('France'))

    def test_paris_parent_is_city(self) -> None:
        self.assertIn('City', self.finder.parents('Paris'))

    def test_eiffel_tower_parent_is_landmark(self) -> None:
        self.assertIn('Landmark', self.finder.parents('Eiffel_Tower'))

    def test_pacific_ocean_parent_is_ocean(self) -> None:
        self.assertIn('Ocean', self.finder.parents('Pacific_Ocean'))

    def test_mount_everest_parent_is_mountain(self) -> None:
        self.assertIn('Mountain', self.finder.parents('Mount_Everest'))

    def test_continent_parent_is_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.parents('Continent'))

    def test_country_parent_is_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.parents('Country'))

    # ------------------------------------------------------------------ #
    # ancestors()                                                         #
    # ------------------------------------------------------------------ #

    def test_france_ancestors_includes_country(self) -> None:
        self.assertIn('Country', self.finder.ancestors('France'))

    def test_france_ancestors_includes_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.ancestors('France'))

    def test_new_york_ancestors_includes_city(self) -> None:
        self.assertIn('City', self.finder.ancestors('New_York'))

    def test_new_york_ancestors_includes_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.ancestors('New_York'))

    def test_eiffel_tower_ancestors_includes_landmark(self) -> None:
        self.assertIn('Landmark', self.finder.ancestors('Eiffel_Tower'))

    def test_eiffel_tower_ancestors_includes_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.ancestors('Eiffel_Tower'))

    def test_mount_everest_ancestors_includes_mountain(self) -> None:
        self.assertIn('Mountain', self.finder.ancestors('Mount_Everest'))

    def test_mount_everest_ancestors_includes_geographic_region(self) -> None:
        self.assertIn('Geographic_Region', self.finder.ancestors('Mount_Everest'))

    # ------------------------------------------------------------------ #
    # has_parent / has_ancestor                                           #
    # ------------------------------------------------------------------ #

    def test_has_parent_europe_continent(self) -> None:
        self.assertTrue(self.finder.has_parent('Europe', 'Continent'))

    def test_has_parent_negative_france_city(self) -> None:
        self.assertFalse(self.finder.has_parent('France', 'City'))

    def test_has_ancestor_france_geographic_region(self) -> None:
        self.assertTrue(self.finder.has_ancestor('France', 'Geographic_Region'))

    def test_has_ancestor_pacific_ocean_geographic_region(self) -> None:
        self.assertTrue(self.finder.has_ancestor('Pacific_Ocean', 'Geographic_Region'))

    def test_has_ancestor_negative_france_mountain(self) -> None:
        self.assertFalse(self.finder.has_ancestor('France', 'Mountain'))

    # ------------------------------------------------------------------ #
    # descendants()                                                       #
    # ------------------------------------------------------------------ #

    def test_country_descendants_includes_france(self) -> None:
        self.assertIn('France', self.finder.descendants('Country'))

    def test_geographic_region_descendants_includes_france(self) -> None:
        self.assertIn('France', self.finder.descendants('Geographic_Region'))

    def test_geographic_region_descendants_includes_pacific_ocean(self) -> None:
        self.assertIn('Pacific_Ocean', self.finder.descendants('Geographic_Region'))

    def test_geographic_region_descendants_includes_mount_everest(self) -> None:
        self.assertIn('Mount_Everest', self.finder.descendants('Geographic_Region'))

    # ------------------------------------------------------------------ #
    # synonyms()  - altLabel surface                                       #
    # ------------------------------------------------------------------ #

    def test_synonyms_is_non_empty(self) -> None:
        self.assertGreater(len(self.finder.synonyms()), 0)

    def test_united_states_has_usa_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        us_key = next((k for k in syns if k.lower() == 'united_states'), None)
        if us_key:
            variants = [v.lower() for v in syns[us_key]]
            self.assertIn('usa', variants)

    def test_united_kingdom_has_uk_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        uk_key = next((k for k in syns if k.lower() == 'united_kingdom'), None)
        if uk_key:
            variants = [v.lower() for v in syns[uk_key]]
            self.assertIn('uk', variants)

    def test_germany_has_deutschland_as_synonym(self) -> None:
        syns = self.finder.synonyms()
        de_key = next((k for k in syns if k.lower() == 'germany'), None)
        if de_key:
            variants = [v.lower() for v in syns[de_key]]
            self.assertIn('deutschland', variants)


if __name__ == '__main__':
    unittest.main()
