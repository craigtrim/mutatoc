"""Persistent RDFLib query backend. Ontology parsing and Mutato matching remain in C."""
import argparse,contextlib,json,os,sys


def send(value):
    sys.stdout.write(json.dumps(value,ensure_ascii=False,allow_nan=False)+"\n")
    sys.stdout.flush()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--model')  # Shared native worker transport argument.
    parser.parse_args()
    sys.stdin.reconfigure(encoding='utf-8')
    sys.stdout.reconfigure(encoding='utf-8',newline='\n')
    sys.stderr.reconfigure(encoding='utf-8',errors='backslashreplace')
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import rdflib
        send({'ok':True,'result':{'protocol':1,'pid':os.getpid(),'rdflib':rdflib.__version__}})
    except Exception as exc:
        send({'ok':False,'error':{'message':f'SPARQL worker initialization: {exc}'}})
        return 1
    graph=None
    def term(value):
        if value['kind']=='iri':return rdflib.URIRef(value['value'])
        if value['kind']=='blank':return rdflib.BNode(value['value'][2:])
        return rdflib.Literal(value['value'],lang=value.get('language'),datatype=None if value.get('language') else value.get('datatype'))
    def encoded(value):
        if value is None:return None
        if isinstance(value,rdflib.URIRef):return {'kind':'iri','value':str(value)}
        if isinstance(value,rdflib.BNode):return {'kind':'blank','value':'_:'+str(value)}
        result={'kind':'literal','value':str(value)}
        if value.datatype:result['datatype']=str(value.datatype)
        if value.language:result['language']=value.language
        return result
    for line in sys.stdin:
        try:
            q=json.loads(line)
            if q.get('op')=='normalize_literals':
                with contextlib.redirect_stdout(sys.stderr):
                    values=[str(term(v)) for v in q['values']]
                send({'ok':True,'result':values})
                continue
            if q.get('op')!='sparql':raise ValueError('Expected sparql operation')
            with contextlib.redirect_stdout(sys.stderr):
                if 'graph' in q:
                    candidate=rdflib.Graph()
                    for triple in q['graph']:candidate.add(tuple(term(triple[k]) for k in ['subject','predicate','object']))
                    for prefix,namespace in q.get('prefixes',{}).items():candidate.bind(prefix,namespace,replace=True)
                    graph=candidate
                if graph is None:raise ValueError('No RDF graph loaded')
                args=q.get('args',[]);kw=q.get('kwargs',{})
                query=q.get('query',kw.get('sparql_query',args[0] if args else None))
                if not isinstance(query,str):raise ValueError('SPARQL requires a query string')
                result=graph.query(query)
                if result.type in ['SELECT','ASK']:
                    raw=json.loads(result.serialize(format='json'))
                else:
                    raw={'type':result.type,'triples':[dict(zip(['subject','predicate','object'],map(encoded,t))) for t in result.graph]}
                rows=[] if result.type=='ASK' else [[encoded(v) for v in row] for row in result]
                payload={'raw':raw,'rows':rows,'type':result.type,'count':len(result),'empty':not result or not len(result)}
            send({'ok':True,'result':payload})
        except Exception as exc:
            send({'ok':False,'error':{'message':f'SPARQL query failed: {type(exc).__name__}: {exc}'}})
    return 0


if __name__=='__main__':
    raise SystemExit(main())
