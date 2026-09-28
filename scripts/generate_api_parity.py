"""Record every public query facade against the original Python implementation."""
import inspect
import json
import os
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.reference/src'))
os.environ['SPAN_DISTANCE']='4'
from mutato.finder.multiquery import FindOntologyData, FindOntologyJSON
from mutato.finder.singlequery import AskOwlAPI
from mutato.finder.singlequery.bp import AskJsonAPI
from mutato.mda import MDAGenerator

OUT=ROOT/'tests/fixtures/api'
entities=['Dog','dog','Domestic dog','dog_collar','Unlabelled','Root','missing','']
noargs={'ontologies','absolute_path','predicates','labels','labels_rev','keyed_labels','types','types_rev','entities','trie','synonyms','synonyms_rev','synonyms_lookup','lookup','spans','span_keys','has_spans','has_data','effects','effects_rev','requires','required_by','similar','similar_rev','implies','implied_by','uses','uses_rev','graffl_ner','graffl_ner_rev','spacy_ner','spacy_ner_rev','ner_depth','ner_depth_rev','ner_taxonomy','ner_taxonomy_rev','infer_by_requires'}
byentity={'is_canon','find_canon','is_variant','find_variants','find_ner','label_by_entity','entity_exists','children','children_and_self','descendants','descendants_and_self','parents','parents_and_self','ancestors','ancestors_and_self','requires_by_entity','required_by_entity','similar_by_entity','implies_by_entity','implied_by_entity'}
rows=[]
for name,folder in [('contract',OUT),('proper',OUT),('animals-test',ROOT/'tests/fixtures/ontologies')]:
    snapshot=MDAGenerator(name,str(folder),None).generate()
    (OUT/(name+'.snapshot.json')).write_text(json.dumps(snapshot,ensure_ascii=False),encoding='utf-8')
    interfaces={'owl':AskOwlAPI(name,str(folder),None),'data':FindOntologyData([name],str(folder),None),'json':FindOntologyJSON(snapshot,name),'ask_json':AskJsonAPI(snapshot)}
    for interface,obj in interfaces.items():
        for method in sorted(n for n,v in inspect.getmembers(type(obj),callable) if not n.startswith('_')):
            if method=='adhoc':continue # Separate SPARQL corpus covers this endpoint.
            calls=[]
            if method in noargs:calls=[([], {})]
            elif method in byentity:calls=[([e],{}) for e in entities]
            elif method in ('has_parent','has_ancestor'):calls=[([e,'animal'],{}) for e in entities if e]
            elif method in ('by_predicate','by_predicate_rev'):calls=[([p],{}) for p in ['rdfs:subClassOf','rdfs:label','requires',':requires','rdfs:comment',':missing']]
            elif method=='ngrams':calls=[([n],{}) for n in [1,2,5,10]]
            elif method=='equivalents':
                calls=[([], {})] if interface in ('owl','ask_json') else [([['pet','companion','missing']],{}),([['pet']],{'flat_list':False}),([['pet']],{'underscore_entities':False})]
            elif method=='transitive':calls=[(['dog'],{'query':'requires_by_entity'})]
            else:raise AssertionError(f'Missing audit invocation: {interface}.{method}')
            if interface=='data' and method in ('labels','labels_rev'):calls.append(([],{'force_lowercase':False}))
            if interface=='owl' and method=='types':calls.append(([],{'to_lowercase':False}))
            if interface in ('owl','data') and method in ('by_predicate','by_predicate_rev'):calls.append((['rdfs:label'],{'to_lowercase':False}))
            for args,kwargs in calls:
                q={'op':'query','interface':interface,'method':method,'args':args,'kwargs':kwargs}
                row={'fixture':name,'request':q}
                try:
                    target=FindOntologyData([name],str(folder),None) if interface=='data' else obj
                    kw=dict(kwargs)
                    if method=='transitive':kw['query']=getattr(target,kw['query'])
                    value=json.loads(json.dumps(getattr(target,method)(*args,**kw)))
                    if method=='absolute_path':value='$FIXTURE_DIRECTORY'
                    row['expected']=value
                except Exception as exc:row['error']=type(exc).__name__
                rows.append(row)
(OUT/'queries.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'Recorded {len(rows)} public API calls across {len(set((r["request"]["interface"],r["request"]["method"]) for r in rows))} methods.')
