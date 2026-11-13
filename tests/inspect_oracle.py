"""Capture or compare exact producer bytes around a formatting-only rewrite."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
mode, directory = sys.argv[1:]
directory = Path(directory).resolve()
assert mode in ('capture', 'compare')
binary = os.environ.get('HIBR', str(root / 'build/hibr'))
env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(root / 'build/mods')))
files = subprocess.check_output(['git', 'ls-files', '*.hibr'], cwd=root, text=True).splitlines()
manifest = {}
for name in files:
    path = root / name
    manifest[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for request in ('functions', 'calls', 'refs', 'check'):
        result = subprocess.run([binary, '-c', 'need inspect; inspect "$1" "$2" -j', '_', request, str(path)],
                                cwd=root, env=env, capture_output=True, timeout=10)
        assert result.returncode == 0 and not result.stderr, (name, request, result.stderr)
        target = directory / request / name
        if mode == 'capture':
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(result.stdout)
        else:
            assert target.read_bytes() == result.stdout, (name, request, 'producer bytes changed')
if mode == 'capture':
    (directory / 'manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
else:
    assert json.loads((directory / 'manifest.json').read_text()) == manifest, 'source corpus changed'
print(f'{mode}: {len(files) * 4} exact outputs over {len(files)} files')
