"""Compare mutatoc with the Python Mutato reference on identical inputs.

Run with a Python that has Mutato's pinned dependencies, and point it at a
Mutato checkout and a mutatoc executable:

    python scripts/benchmark_mutato.py --mutato .reference/bench/mutato --exe dist/mutatoc-win-x64-0.2.2/mutatoc.exe

Every measurement runs both implementations on the same ontology and input.
Construction is timed in a fresh process per iteration so neither side gains
from warm caches. mutatoc is driven through --serve, so its figures include
JSON encoding and pipe transfer.
"""
import argparse, copy, json, os, platform, statistics, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/fixtures'
ONTOLOGIES = [
    ('animals-test', ['animals-test']),
    ('econ-20160218', ['econ-20160218']),
    ('medicopilot', ['medicopilot', 'medic-copilot-20230726']),
    ('courses-20251028', ['courses-20240709', 'courses-20241023', 'courses-20241129', 'courses-20241213',
                          'courses-20250122', 'courses-20250131', 'courses-20251028']),
]
DOCUMENT_CHARS = 2400

BUILD_CHILD = r'''
import sys, time
sys.path.insert(0, sys.argv[1])
from mutato.api import OntologyParser
import mutato.mda.universal_mda_generator, mutato.finder.multiquery.bp, mutato.parser
start = time.perf_counter()
OntologyParser(sys.argv[2])
print(time.perf_counter() - start)
'''

CLI_CHILD = r'''
import sys
sys.path.insert(0, sys.argv[1])
from mutato.api import OntologyParser
print(OntologyParser(sys.argv[2]).parse(sys.argv[3]))
'''


class Server:
    def __init__(self, exe):
        self.p = subprocess.Popen([str(exe), '--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, encoding='utf-8')

    def call(self, **request):
        self.p.stdin.write(json.dumps(request) + '\n')
        self.p.stdin.flush()
        response = json.loads(self.p.stdout.readline())
        if not response.get('ok'):
            raise RuntimeError(response.get('error'))
        return response['result']

    def close(self):
        self.p.stdin.close()
        self.p.wait(30)


def timed(fn):
    start = time.perf_counter()
    value = fn()
    return time.perf_counter() - start, value


def summary(samples):
    return {'median_ms': round(statistics.median(samples) * 1000, 2), 'min_ms': round(min(samples) * 1000, 2),
            'runs': len(samples)}


def cases_for(names):
    rows = []
    for name in names:
        path = FIXTURES / f'parity/{name}.cases.json'
        if path.exists():
            rows += json.loads(path.read_text(encoding='utf-8'))
    return rows


def document_for(cases):
    # Drop trailing periods so sentences never end in "..", which the two
    # implementations deliberately render differently.
    phrases = list(dict.fromkeys(c['text'].strip().rstrip('.') for c in cases if c['text'].strip().rstrip('.')))
    parts, size, i = [], 0, 0
    while size < DOCUMENT_CHARS:
        phrase = phrases[i % len(phrases)]
        sentence = f'The report mentions {phrase}.'
        parts.append(sentence)
        size += len(sentence) + 1
        i += 1
    return ' '.join(parts)


def canonical(tokens):
    return ' '.join(t['swaps']['canon'] if t.get('swaps') else t['text'].strip()
                    for t in tokens if t.get('swaps') or t['text'].strip())


def bench(args, name, case_names, python):
    owl = FIXTURES / f'ontologies/{name}.owl'
    cases = cases_for(case_names)
    document = document_for(cases)
    result = {'ontology': owl.name, 'owl_bytes': owl.stat().st_size, 'cases': len(cases),
              'document_chars': len(document)}
    mutato_path = str(args.mutato.resolve())

    # Construction from OWL, one fresh process per sample on both sides.
    py_build, c_build = [], []
    for _ in range(args.build_runs):
        out = subprocess.run([python, '-c', BUILD_CHILD, mutato_path, str(owl)], capture_output=True,
                             text=True, check=True)
        py_build.append(float(out.stdout.strip().splitlines()[-1]))
        server = Server(args.exe)
        c_build.append(timed(lambda: server.call(op='load', path=str(owl)))[0])
        server.close()
    result['build_from_owl'] = {'mutato': summary(py_build), 'mutatoc': summary(c_build)}

    # Warm in-process comparisons.
    sys.path.insert(0, mutato_path)
    from mutato.api import OntologyParser
    from mutato.finder.multiquery.bp import FindOntologyJSON
    from mutato.parser import MutatoAPI
    snapshot = OntologyParser(str(owl)).to_dict()
    snapshot_text = json.dumps(snapshot)
    server = Server(args.exe)
    server.call(op='load', path=str(owl))

    py_restore, c_restore = [], []
    for _ in range(args.runs):
        py_restore.append(timed(lambda: OntologyParser.from_dict(json.loads(snapshot_text), name=name))[0])
        c_restore.append(timed(lambda: server.call(op='load', snapshot=snapshot, name=name))[0])
    result['restore_from_snapshot'] = {'mutato': summary(py_restore), 'mutatoc': summary(c_restore)}

    api = MutatoAPI(find_ontology_data=FindOntologyJSON(d_owl=snapshot, ontology_name=name))
    py_match, c_match, mismatches = [], [], 0
    for round_ in range(args.runs):
        py_total = c_total = 0.0
        for case in cases:
            elapsed, expected = timed(lambda: api.swap_input_tokens(tokens=copy.deepcopy(case['tokens']),
                                                                    ctr=case['ctr']))
            py_total += elapsed
            elapsed, actual = timed(lambda: server.call(op='parse_tokens', tokens=case['tokens'], ctr=case['ctr']))
            c_total += elapsed
            if round_ == 0 and canonical(expected or []) != canonical(actual['tokens']):
                mismatches += 1
        py_match.append(py_total)
        c_match.append(c_total)
    result['match_all_cases'] = {'mutato': summary(py_match), 'mutatoc': summary(c_match),
                                 'output_mismatches': mismatches}

    # Warm raw-text parsing; model startup is covered by the cold run below.
    py_tokens = api.swap_input_text(document)
    c_result = server.call(op='parse', text=document)
    py_parse, c_parse = [], []
    for _ in range(args.runs):
        py_parse.append(timed(lambda: api.swap_input_text(document))[0])
        c_parse.append(timed(lambda: server.call(op='parse', text=document))[0])
    result['parse_document'] = {'mutato': summary(py_parse), 'mutatoc': summary(c_parse),
                                'same_output': canonical(py_tokens) == canonical(c_result['tokens'])}
    server.close()

    # Cold one-shot runs: process start, model load, construction and one parse.
    phrase = cases[0]['text']
    py_cli, c_cli, same_cli = [], [], True
    for _ in range(args.build_runs):
        elapsed, py_out = timed(lambda: subprocess.run([python, '-c', CLI_CHILD, mutato_path, str(owl), phrase],
                                                       capture_output=True, text=True, encoding='utf-8', check=True))
        py_cli.append(elapsed)
        elapsed, c_out = timed(lambda: subprocess.run([str(args.exe), '--ontology', str(owl), '--input-text', phrase],
                                                      capture_output=True, text=True, encoding='utf-8', check=True))
        c_cli.append(elapsed)
        same_cli = same_cli and py_out.stdout.strip() == c_out.stdout.strip()
    result['cold_cli'] = {'mutato': summary(py_cli), 'mutatoc': summary(c_cli), 'same_output': same_cli}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutato', type=Path, required=True, help='Mutato source checkout')
    parser.add_argument('--exe', type=Path, required=True, help='mutatoc executable')
    parser.add_argument('--runs', type=int, default=10)
    parser.add_argument('--build-runs', type=int, default=3)
    parser.add_argument('--only', nargs='*')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/benchmark-mutato.json')
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    revision = subprocess.run(['git', '-C', str(args.mutato), 'rev-parse', 'HEAD'], capture_output=True,
                              text=True).stdout.strip()
    version = subprocess.run([str(args.exe), '--version'], capture_output=True, text=True).stdout.strip()
    report = {'mutato_revision': revision, 'mutatoc': version, 'python': sys.version.split()[0],
              'platform': platform.platform(), 'processor': platform.processor(), 'results': []}
    for name, case_names in ONTOLOGIES:
        if args.only and name not in args.only:
            continue
        print(f'benchmarking {name}', file=sys.stderr, flush=True)
        report['results'].append(bench(args, name, case_names, sys.executable))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
