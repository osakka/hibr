"""Source evidence identifies executable syntax and survives extraction."""
from pathlib import Path
import tempfile
import unittest

from inspectlib import records


class SourceSpans(unittest.TestCase):
    def inspect(self, source, request='calls'):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.hibr'
            path.write_text(source)
            result = records(path, request)
            self.assertEqual(result['state'], 'ok', result)
            return result[request]

    def text(self, source, record):
        span = record['span']
        return source.encode()[span['start']:span['end']].decode()

    def test_nested_call_does_not_point_into_literal_data(self):
        source = 'fn outer() { echo \'$(touch marker)\' "$(touch marker)"; }\n'
        call = next(c for c in self.inspect(source) if c['name'] == 'touch')
        self.assertEqual(call['span']['start'], source.index('"$(touch marker)"') + 3)
        self.assertEqual(self.text(source, call), 'touch')

    def test_command_words_distinct_from_identical_arguments(self):
        source = 'echo echo; echo "echo"; "${tool}_run" "private"\n'
        calls = self.inspect(source)
        self.assertEqual([c['span']['start'] for c in calls], [0, 11, 24])
        self.assertEqual([self.text(source, c) for c in calls], ['echo', 'echo', '"${tool}_run"'])

    def test_substitutions_and_process_substitutions(self):
        source = 'echo "$(echo \'$(printf hidden)\' "$(printf real)")" <(printf input)\n'
        calls = self.inspect(source)
        prints = [c for c in calls if c['name'] == 'printf']
        self.assertEqual([c['span']['start'] for c in prints],
                         [source.index('printf real'), source.index('printf input')])
        self.assertTrue(all(self.text(source, c) == 'printf' for c in prints))

    def test_transformed_backtick_is_explicitly_partial(self):
        source = 'echo `printf \\$secret`\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.hibr'
            path.write_text(source)
            result = records(path, 'calls')
            self.assertTrue(result['partial'])
            unresolved = next(c for c in result['calls'] if c.get('reason') == 'transformed-or-deep-substitution')
            self.assertEqual(self.text(source, unresolved), 'printf \\$secret')
            self.assertFalse(any(c['name'] == 'printf' for c in result['calls']))

    def test_reference_points_to_argument(self):
        source = 'fn callback() { :; }; echo callback "callback"\n'
        refs = self.inspect(source, 'refs')
        self.assertEqual([self.text(source, c) for c in refs], ['callback', '"callback"'])

    def test_endline_is_last_covered_line(self):
        source = '# author intent\nfn example() {\n :;\n}\n'
        function = self.inspect(source, 'functions')[0]
        summary = function['summary']['span']
        self.assertEqual(summary['end'], source.index('fn'))
        self.assertEqual(summary['endline'], 1)
        self.assertEqual(function['body']['span']['endline'], 4)
        self.assertEqual(function['span']['endline'], 4)

    def test_parameter_defaults_are_declared_unevaluated_evidence(self):
        source = 'fn example(int n=3, str label="hello", value=$(echo secret), ...args) { :; }\n'
        parameters = self.inspect(source, 'functions')[0]['params']
        self.assertEqual([p['default']['text'] if p['default'] else None for p in parameters],
                         ['3', '"hello"', '$(echo secret)', None])
        for p in parameters[:-1]:
            self.assertFalse(p['default']['evaluated'])
            self.assertEqual(p['default']['known'], 'declared')
            self.assertEqual(self.text(source, p['default']), p['default']['text'])
        self.assertTrue(parameters[-1]['rest'])

    def test_function_extraction_round_trip(self):
        source = '# author intent\nfn first(int n=3, str value="hello") { echo "$n"; }\nfn second() { first 4; }\n'
        for function in self.inspect(source, 'functions'):
            fragment = self.text(source, function)
            extracted = self.inspect(fragment, 'functions')
            self.assertEqual(len(extracted), 1)
            other = extracted[0]
            self.assertEqual(other['name'], function['name'])
            self.assertEqual(self.text(fragment, other), fragment)
            for before, after in zip(function['params'], other['params']):
                self.assertEqual(before['name'], after['name'])
                self.assertEqual(self.text(source, before), self.text(fragment, after))
                if before.get('default') is not None:
                    self.assertEqual(self.text(source, before['default']), before['default']['text'])
            self.assertTrue(self.text(source, function['body']).startswith('{'))


if __name__ == '__main__':
    unittest.main()
