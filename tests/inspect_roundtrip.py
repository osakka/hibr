"""Every tracked function can be extracted and inspected independently."""
from pathlib import Path
import subprocess
import tempfile

from inspectlib import records

count = 0
with tempfile.TemporaryDirectory() as directory:
    extracted = Path(directory) / 'function.hibr'
    for name in subprocess.check_output(['git', 'ls-files', '*.hibr'], text=True).splitlines():
        path = Path(name)
        source = path.read_bytes()
        for function in records(path, 'functions')['functions']:
            span = function['span']
            fragment = source[span['start']:span['end']]
            extracted.write_bytes(fragment)
            result = records(extracted, 'functions')
            assert result['state'] == 'ok', (name, function['name'], result)
            other = result['functions'][0]
            assert other['name'] == function['name'], (name, function['name'], other)
            assert other['span']['start'] == 0 and other['span']['end'] == len(fragment), (name, function['name'], other['span'])
            count += 1
print(f'{count} whole-tree function extraction round trips passed')
