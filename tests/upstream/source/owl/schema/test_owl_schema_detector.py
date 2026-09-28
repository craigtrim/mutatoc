#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Issue: https://github.com/craigtrim/mutato/issues/4
# Docs: docs/architecture.md
# Tests OWLSchemaDetector against all four test ontologies and econ.owl.
# Verifies deterministic, correct schema detection with zero consumer config.

import unittest
from rdflib import Graph

from mutato.mda.owl_schema import OWLSchema
from mutato.mda.owl_schema_detector import OWLSchemaDetector

ANIMALS_OWL = 'tests/test_data/ontologies/animals-test.owl'
COLORS_OWL = 'tests/test_data/ontologies/colors-test.owl'
MUSIC_OWL = 'tests/test_data/ontologies/music-test.owl'
GEOGRAPHY_OWL = 'tests/test_data/ontologies/geography-test.owl'
ECON_OWL = 'tests/test_data/ontologies/econ-20160218.owl'


def _load(path: str) -> Graph:
    g = Graph()
    g.parse(path, format='ttl')
    return g


class TestOWLSchemaDetector(unittest.TestCase):

    # ------------------------------------------------------------------ #
    # Return type                                                          #
    # ------------------------------------------------------------------ #

    def test_returns_owl_schema_instance_for_animals(self) -> None:
        schema = OWLSchemaDetector(_load(ANIMALS_OWL)).detect()
        self.assertIsInstance(schema, OWLSchema)

    def test_returns_owl_schema_instance_for_colors(self) -> None:
        schema = OWLSchemaDetector(_load(COLORS_OWL)).detect()
        self.assertIsInstance(schema, OWLSchema)

    def test_returns_owl_schema_instance_for_music(self) -> None:
        schema = OWLSchemaDetector(_load(MUSIC_OWL)).detect()
        self.assertIsInstance(schema, OWLSchema)

    def test_returns_owl_schema_instance_for_geography(self) -> None:
        schema = OWLSchemaDetector(_load(GEOGRAPHY_OWL)).detect()
        self.assertIsInstance(schema, OWLSchema)

    def test_returns_owl_schema_instance_for_econ(self) -> None:
        schema = OWLSchemaDetector(_load(ECON_OWL)).detect()
        self.assertIsInstance(schema, OWLSchema)

    # ------------------------------------------------------------------ #
    # Correct detection  - controlled vocabulary ontologies                 #
    # ------------------------------------------------------------------ #

    def test_animals_detected_as_class_based(self) -> None:
        schema = OWLSchemaDetector(_load(ANIMALS_OWL)).detect()
        self.assertEqual(schema, OWLSchema.CLASS_BASED)

    def test_colors_detected_as_class_based(self) -> None:
        schema = OWLSchemaDetector(_load(COLORS_OWL)).detect()
        self.assertEqual(schema, OWLSchema.CLASS_BASED)

    def test_music_detected_as_class_based(self) -> None:
        schema = OWLSchemaDetector(_load(MUSIC_OWL)).detect()
        self.assertEqual(schema, OWLSchema.CLASS_BASED)

    def test_geography_detected_as_class_based(self) -> None:
        schema = OWLSchemaDetector(_load(GEOGRAPHY_OWL)).detect()
        self.assertEqual(schema, OWLSchema.CLASS_BASED)

    # ------------------------------------------------------------------ #
    # Correct detection  - mixed class/individual ontology                  #
    # ------------------------------------------------------------------ #

    def test_econ_detected_as_mixed(self) -> None:
        schema = OWLSchemaDetector(_load(ECON_OWL)).detect()
        self.assertEqual(schema, OWLSchema.MIXED)

    def test_econ_not_detected_as_class_based(self) -> None:
        schema = OWLSchemaDetector(_load(ECON_OWL)).detect()
        self.assertNotEqual(schema, OWLSchema.CLASS_BASED)

    def test_econ_not_detected_as_skos(self) -> None:
        schema = OWLSchemaDetector(_load(ECON_OWL)).detect()
        self.assertNotEqual(schema, OWLSchema.SKOS)

    def test_econ_not_detected_as_individual(self) -> None:
        schema = OWLSchemaDetector(_load(ECON_OWL)).detect()
        self.assertNotEqual(schema, OWLSchema.INDIVIDUAL)

    # ------------------------------------------------------------------ #
    # Determinism  - same graph always returns same schema                  #
    # ------------------------------------------------------------------ #

    def test_animals_detection_is_deterministic(self) -> None:
        g = _load(ANIMALS_OWL)
        result_a = OWLSchemaDetector(g).detect()
        result_b = OWLSchemaDetector(g).detect()
        self.assertEqual(result_a, result_b)

    def test_econ_detection_is_deterministic(self) -> None:
        g = _load(ECON_OWL)
        result_a = OWLSchemaDetector(g).detect()
        result_b = OWLSchemaDetector(g).detect()
        self.assertEqual(result_a, result_b)

    # ------------------------------------------------------------------ #
    # Synthetic edge cases                                                 #
    # ------------------------------------------------------------------ #

    def test_empty_graph_returns_class_based(self) -> None:
        schema = OWLSchemaDetector(Graph()).detect()
        self.assertEqual(schema, OWLSchema.CLASS_BASED)

    def test_skos_concept_triggers_skos_schema(self) -> None:
        from rdflib import URIRef, RDF
        from rdflib.namespace import SKOS
        g = Graph()
        g.add((URIRef('http://example.org/Dog'), RDF.type, SKOS.Concept))
        schema = OWLSchemaDetector(g).detect()
        self.assertEqual(schema, OWLSchema.SKOS)

    def test_individual_only_triggers_individual_schema(self) -> None:
        from rdflib import URIRef, RDF, OWL
        g = Graph()
        g.add((URIRef('http://example.org/Dog'), RDF.type, OWL.NamedIndividual))
        schema = OWLSchemaDetector(g).detect()
        self.assertEqual(schema, OWLSchema.INDIVIDUAL)

    def test_individual_plus_subclass_triggers_mixed_schema(self) -> None:
        from rdflib import URIRef, RDF, OWL, RDFS
        g = Graph()
        g.add((URIRef('http://example.org/Dog'), RDF.type, OWL.NamedIndividual))
        g.add((URIRef('http://example.org/Poodle'), RDFS.subClassOf, URIRef('http://example.org/Dog')))
        schema = OWLSchemaDetector(g).detect()
        self.assertEqual(schema, OWLSchema.MIXED)

    def test_skos_takes_priority_over_individuals(self) -> None:
        from rdflib import URIRef, RDF, OWL
        from rdflib.namespace import SKOS
        g = Graph()
        g.add((URIRef('http://example.org/Dog'), RDF.type, OWL.NamedIndividual))
        g.add((URIRef('http://example.org/Cat'), RDF.type, SKOS.Concept))
        schema = OWLSchemaDetector(g).detect()
        self.assertEqual(schema, OWLSchema.SKOS)


if __name__ == '__main__':
    unittest.main()
