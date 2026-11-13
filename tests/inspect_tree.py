"""Check the tracked Hibr corpus and measure process-inclusive inspection time."""
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parents[1]
env = dict(os.environ, HIBR_MODPATH=str(root / 'build/mods'))
files = subprocess.check_output(['git', 'ls-files', '*.hibr'], cwd=root, text=True).splitlines()
maximum = (0.0, '')
functions = 0
for name in files:
    start = time.perf_counter()
    proc = subprocess.run([str(root / 'build/hibr'), '-c',
                           'need inspect; inspect functions "$1" -j', '_', str(root / name)],
                          env=env, capture_output=True, text=True, timeout=10)
    elapsed = (time.perf_counter() - start) * 1000
    maximum = max(maximum, (elapsed, name))
    result = json.loads(proc.stdout)
    assert result['state'] in ('ok', 'empty'), (name, result)
    assert not proc.stderr, (name, proc.stderr)
    source = (root / name).read_bytes()
    for record in result['functions']:
        span = record['span']
        assert 0 <= span['start'] < span['end'] <= len(source), (name, record)
    functions += len(result['functions'])
print(f'{len(files)} files, {functions} function records; no parse errors or stderr')
print(f'Slowest process-inclusive inspection: {maximum[0]:.2f} ms ({maximum[1]}); callback budget: 50 ms')
