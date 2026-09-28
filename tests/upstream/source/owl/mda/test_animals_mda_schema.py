#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# Validates the schema and structure of the MDA dict produced by
# MDAGenerator for the animals-test ontology.
# Keys: children, parents, trie, ngrams, spans, labels, equivalents,
#       predicates, by_predicate, ner, synonyms{lookup,fwd,rev}.

import unittest
from mutato.mda import MDAGenerator

ONTOLOGY_NAME = 'animals-test'
ABSOLUTE_PATH = 'tests/test_data/ontologies'
NAMESPACE = 'http://test.ai/animals'


class TestAnimalsMDASchema(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        cls.d_owl = MDAGenerator(
            ontology_name=ONTOLOGY_NAME,
            absolute_path=ABSOLUTE_PATH,
            namespace=NAMESPACE,
        ).generate()

    # ------------------------------------------------------------------ #
    # Top-level key presence                                              #
    # ------------------------------------------------------------------ #

    def test_has_children_key(self) -> None:
        self.assertIn('children', self.d_owl)

    def test_has_parents_key(self) -> None:
        self.assertIn('parents', self.d_owl)

    def test_has_trie_key(self) -> None:
        self.assertIn('trie', self.d_owl)

    def test_has_ngrams_key(self) -> None:
        self.assertIn('ngrams', self.d_owl)

    def test_has_spans_key(self) -> None:
        self.assertIn('spans', self.d_owl)

    def test_has_labels_key(self) -> None:
        self.assertIn('labels', self.d_owl)

    def test_has_equivalents_key(self) -> None:
        self.assertIn('equivalents', self.d_owl)

    def test_has_predicates_key(self) -> None:
        self.assertIn('predicates', self.d_owl)

    def test_has_by_predicate_key(self) -> None:
        self.assertIn('by_predicate', self.d_owl)

    def test_has_ner_key(self) -> None:
        self.assertIn('ner', self.d_owl)

    def test_has_synonyms_key(self) -> None:
        self.assertIn('synonyms', self.d_owl)

    # ------------------------------------------------------------------ #
    # Top-level type checks                                               #
    # ------------------------------------------------------------------ #

    def test_children_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['children'], dict)

    def test_parents_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['parents'], dict)

    def test_ngrams_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['ngrams'], dict)

    def test_spans_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['spans'], dict)

    def test_labels_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['labels'], dict)

    def test_ner_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['ner'], dict)

    def test_synonyms_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['synonyms'], dict)

    def test_predicates_is_list(self) -> None:
        self.assertIsInstance(self.d_owl['predicates'], list)

    def test_by_predicate_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['by_predicate'], dict)

    # ------------------------------------------------------------------ #
    # children dict structure                                             #
    # ------------------------------------------------------------------ #

    def test_children_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['children']), 0)

    def test_children_values_are_lists(self) -> None:
        for v in self.d_owl['children'].values():
            self.assertIsInstance(v, list)
            break  # check first entry only

    def test_children_animal_includes_mammal(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Animal', children)
        self.assertIn('Mammal', children['Animal'])

    def test_children_mammal_includes_dog(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Mammal', children)
        self.assertIn('Dog', children['Mammal'])

    def test_children_dog_includes_poodle(self) -> None:
        children = self.d_owl['children']
        self.assertIn('Dog', children)
        self.assertIn('Poodle', children['Dog'])

    # ------------------------------------------------------------------ #
    # parents dict structure                                              #
    # ------------------------------------------------------------------ #

    def test_parents_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['parents']), 0)

    def test_parents_values_are_lists(self) -> None:
        for v in self.d_owl['parents'].values():
            self.assertIsInstance(v, list)
            break

    def test_parents_poodle_includes_dog(self) -> None:
        parents = self.d_owl['parents']
        self.assertIn('Poodle', parents)
        self.assertIn('Dog', parents['Poodle'])

    def test_parents_dog_includes_mammal(self) -> None:
        parents = self.d_owl['parents']
        self.assertIn('Dog', parents)
        self.assertIn('Mammal', parents['Dog'])

    # ------------------------------------------------------------------ #
    # ngrams structure                                                    #
    # ------------------------------------------------------------------ #

    def test_ngrams_has_unigrams(self) -> None:
        self.assertIn(1, self.d_owl['ngrams'])

    def test_ngrams_has_bigrams(self) -> None:
        self.assertIn(2, self.d_owl['ngrams'])

    def test_ngrams_unigrams_is_list(self) -> None:
        self.assertIsInstance(self.d_owl['ngrams'][1], list)

    def test_ngrams_unigrams_includes_dog(self) -> None:
        unigrams = [t.lower() for t in self.d_owl['ngrams'][1]]
        self.assertIn('dog', unigrams)

    def test_ngrams_bigrams_includes_german_shepherd(self) -> None:
        bigrams = [t.lower() for t in self.d_owl['ngrams'][2]]
        self.assertIn('german_shepherd', bigrams)

    # ------------------------------------------------------------------ #
    # spans structure                                                     #
    # ------------------------------------------------------------------ #

    def test_spans_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['spans']), 0)

    def test_spans_contains_german_shepherd(self) -> None:
        # spans keys are multi-word first tokens (lowercased)
        span_keys = [k.lower() for k in self.d_owl['spans']]
        self.assertIn('german', span_keys)

    def test_spans_contains_blue_whale(self) -> None:
        span_keys = [k.lower() for k in self.d_owl['spans']]
        self.assertIn('blue', span_keys)

    # ------------------------------------------------------------------ #
    # synonyms sub-structure                                              #
    # ------------------------------------------------------------------ #

    def test_synonyms_has_fwd_key(self) -> None:
        self.assertIn('fwd', self.d_owl['synonyms'])

    def test_synonyms_has_rev_key(self) -> None:
        self.assertIn('rev', self.d_owl['synonyms'])

    def test_synonyms_has_lookup_key(self) -> None:
        self.assertIn('lookup', self.d_owl['synonyms'])

    def test_synonyms_fwd_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['synonyms']['fwd'], dict)

    def test_synonyms_rev_is_dict(self) -> None:
        self.assertIsInstance(self.d_owl['synonyms']['rev'], dict)

    def test_synonyms_fwd_dog_has_canine(self) -> None:
        fwd = self.d_owl['synonyms']['fwd']
        dog_key = next((k for k in fwd if k.lower() == 'dog'), None)
        if dog_key:
            variants = [v.lower() for v in fwd[dog_key]]
            self.assertIn('canine', variants)

    # ------------------------------------------------------------------ #
    # ner structure                                                       #
    # ------------------------------------------------------------------ #

    def test_ner_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['ner']), 0)

    def test_ner_values_are_strings(self) -> None:
        for v in self.d_owl['ner'].values():
            self.assertIsInstance(v, str)
            break

    def test_ner_all_values_are_ner_label(self) -> None:
        for v in self.d_owl['ner'].values():
            self.assertEqual(v, 'NER')

    # ------------------------------------------------------------------ #
    # labels structure                                                    #
    # ------------------------------------------------------------------ #

    def test_labels_is_non_empty(self) -> None:
        self.assertGreater(len(self.d_owl['labels']), 0)

    def test_labels_contains_dog(self) -> None:
        label_keys = [k.lower() for k in self.d_owl['labels']]
        self.assertIn('dog', label_keys)

    def test_labels_contains_animal(self) -> None:
        label_keys = [k.lower() for k in self.d_owl['labels']]
        self.assertIn('animal', label_keys)


if __name__ == '__main__':
    unittest.main()
