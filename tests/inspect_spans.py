"""Release regression: source evidence must identify code, never matching data."""
import os
from pathlib import Path
import tempfile
import unittest

from inspectlib import records


class SourceSpans(unittest.TestCase):
    def test_nested_call_does_not_point_into_literal_data(self):
        source = 'fn outer() { echo \'$(touch marker)\' "$(touch marker)"; }\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.hibr'
            path.write_text(source)
            calls = records(path, 'calls')['calls']
            call = next(c for c in calls if c['name'] == 'touch' and c.get('scope') == 'substitution')
            expected = source.index('"$(touch marker)"') + 3
            self.assertEqual(call['span']['start'], expected,
                             'exact nested-command attribution is required; an enclosing-unit span is insufficient')


if __name__ == '__main__':
    unittest.main()
