"""Variable-flow evidence comes from Hibr's parser without running source."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HIBR = os.environ.get('HIBR', str(ROOT / 'build/hibr'))
MODS = os.environ.get('HIBR_TESTMODS', str(ROOT / 'build/mods'))


def inspect(path, function=None):
    command = 'need inspect; inspect "$1" "$2" "$3" -j' if function else \
              'need inspect; inspect "$1" "$2" -j'
    args = [HIBR, '-c', command, '_', 'vars', str(path)]
    if function:
        args.append(function)
    proc = subprocess.run(args, cwd=ROOT,
                          env=dict(os.environ, HIBR_MODPATH=MODS),
                          capture_output=True, text=True, timeout=10)
    if proc.stderr:
        raise AssertionError(proc.stderr)
    return json.loads(proc.stdout)


class VariableInspection(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, source):
        path = self.root / name
        path.write_text(source)
        return path

    def test_records_have_exact_spans_owner_and_supported_kinds(self):
        source = (
            'root=0\n'
            'fn update(int amount) {\n'
            '  local total=1 scratch\n'
            '  total=2\n'
            '  echo "$amount $total"\n'
            '  target := lookup "$total"\n'
            '  for item in a b; do echo "$item"; done\n'
            '  read -p prompt destination\n'
            '  mapfile -t lines\n'
            '  printf -v rendered "%s" "$total"\n'
            '  ((root += total))\n'
            '  ((++total)); ((total++))\n'
            '  for ((index=0; index<total; index++)); do :; done\n'
            '}\n'
        )
        path = self.write('flow.hibr', source)
        result = inspect(path)
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        self.assertEqual(result['source']['size'], len(source.encode()))
        records = result['vars']
        self.assertTrue(records)
        for record in records:
            span = record['span']
            actual = source.encode()[span['start']:span['end']].decode()
            self.assertIn(actual, (record['name'], '$' + record['name']), record)

        kinds = {(r['name'], r['kind']) for r in records}
        for expected in {
            ('root', 'write'), ('amount', 'parameter'),
            ('total', 'declaration'), ('total', 'write'),
            ('amount', 'read'), ('total', 'read'), ('target', 'bind'),
            ('item', 'loop-write'), ('item', 'read'),
            ('destination', 'write'), ('lines', 'write'),
            ('rendered', 'write'), ('index', 'write'),
            ('scratch', 'declaration'),
        }:
            self.assertIn(expected, kinds)
        self.assertEqual(next(r for r in records if r['name'] == 'scratch')['storage'],
                         'local')
        self.assertEqual({r['owner'] for r in records if r['name'] == 'total'},
                         {'update'})
        self.assertEqual({r['scope_kind'] for r in records if r['name'] == 'total'},
                         {'function'})
        self.assertEqual(len({r['scope_id'] for r in records if r['name'] == 'total'}), 1)
        parameter = next(r for r in records if r['kind'] == 'parameter')
        self.assertEqual(parameter['name'], 'amount')
        self.assertEqual(records[0]['owner'], None)
        self.assertEqual(records[0]['scope_kind'], 'module')
        self.assertEqual(records[0]['scope_id'], 'module')

    def test_nested_function_sites_have_distinct_enclosing_scope_ids(self):
        source = ('fn outer() {\n'
                  '  local value=1\n'
                  '  fn inner() { value=2; echo "$value"; }\n'
                  '  value=3\n'
                  '}\n')
        result = inspect(self.write('nested-scope.hibr', source))
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        outer = [r for r in result['vars'] if r['name'] == 'value' and
                 r['owner'] == 'outer']
        inner = [r for r in result['vars'] if r['name'] == 'value' and
                 r['owner'] == 'inner']
        self.assertTrue(outer, result)
        self.assertTrue(inner, result)
        self.assertEqual({r['scope_kind'] for r in outer + inner}, {'function'})
        self.assertEqual(len({r['scope_id'] for r in outer}), 1)
        self.assertEqual(len({r['scope_id'] for r in inner}), 1)
        self.assertNotEqual(outer[0]['scope_id'], inner[0]['scope_id'])

    def test_dynamic_binding_candidates_keep_call_path_and_exclude_unrelated_scope(self):
        source = ('carried=0\n'
                  'fn leaf() { echo "$carried"; }\n'
                  'fn middle() { leaf; }\n'
                  'fn caller() { local carried=8; middle; }\n'
                  'fn unrelated() { local carried=5; }\n')
        result = inspect(self.write('dynamic-binding.hibr', source), 'leaf')
        self.assertEqual(result['state'], 'ok', result)
        reads = [r for r in result['vars'] if r['kind'] == 'read']
        self.assertEqual(len(reads), 1, result)
        analysis = reads[0]['binding_analysis']
        self.assertEqual(analysis['state'], 'candidate-set')
        self.assertFalse(analysis['complete'])
        caller = next(c for c in analysis['candidates']
                      if c['relation'] == 'dynamic-caller')
        self.assertEqual(caller['owner'], 'caller')
        self.assertEqual(caller['name'], 'carried')
        self.assertEqual([edge['name'] for edge in caller['call_path']],
                         ['middle', 'leaf'])
        self.assertTrue(any(c['relation'] == 'possible-global'
                            for c in analysis['candidates']))
        self.assertNotIn('unrelated', {c['owner'] for c in analysis['candidates']})
        self.assertIn('external callers', analysis['unknown'])

    def test_arithmetic_comparison_reads_and_no_false_comparison_write(self):
        source = 'fn f() { ((left < right)); ((left == right)); }\n'
        result = inspect(self.write('compare.hibr', source))
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        pairs = [(r['name'], r['kind']) for r in result['vars']]
        self.assertEqual(pairs, [('left', 'read'), ('right', 'read'),
                                ('left', 'read'), ('right', 'read')])

    def test_arithmetic_expansions_and_parameter_reads(self):
        source = 'fn f() { out=$((plain + ${#text} + $other)); }\n'
        result = inspect(self.write('arithmetic-expansion.hibr', source))
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        pairs = [(r['name'], r['kind']) for r in result['vars']]
        self.assertEqual(pairs, [('out', 'write'), ('plain', 'read'),
                                 ('text', 'read'), ('other', 'read')])
        data = source.encode()
        self.assertEqual([data[r['span']['start']:r['span']['end']].decode()
                          for r in result['vars']],
                         ['out', 'plain', '${#text}', '$other'])
        self.assertTrue(all(r['owner'] == 'f' for r in result['vars']))

    def test_malformed_arithmetic_is_reported_as_partial(self):
        source = 'fn f() { ((known +)); }\n'
        result = inspect(self.write('malformed.hibr', source))
        self.assertEqual(result['state'], 'ok', result)
        self.assertTrue(result['partial'], result)
        self.assertTrue(any(r['name'] == 'known' for r in result['vars']))

    def test_filter_limits_records_to_selected_function(self):
        source = 'fn first() { a=1; }\nfn second() { b=2; }\n'
        result = inspect(self.write('filter.hibr', source), 'second')
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        self.assertEqual([(r['name'], r['owner']) for r in result['vars']],
                         [('b', 'second')])

    def test_filter_keeps_nested_parameter_expansion_reads(self):
        source = ('fn first() { a=$((x + $first_value)); }\n'
                  'fn second() { b=$((y + ${#second_value})); }\n')
        result = inspect(self.write('filtered-expansion.hibr', source), 'second')
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(result['partial'], result)
        self.assertEqual([(r['name'], r['kind'], r['owner'])
                          for r in result['vars']],
                         [('b', 'write', 'second'), ('y', 'read', 'second'),
                          ('second_value', 'read', 'second')])

    def test_repeated_inspection_is_stable(self):
        path = self.write('repeat.hibr', 'x=0\nfn f() { ((x++)); }\n')
        first = inspect(path)
        second = inspect(path)
        self.assertEqual(first, second)

    def test_inspection_never_executes_source(self):
        marker = self.root / 'must-not-exist'
        source = f'fn f() {{ touch {marker}; echo "$(touch {marker})"; }}\n'
        path = self.write('no-execution.hibr', source)
        result = inspect(path)
        self.assertEqual(result['state'], 'ok', result)
        self.assertFalse(marker.exists())


if __name__ == '__main__':
    unittest.main()
