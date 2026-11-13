import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
binary = os.environ.get('HIBR', str(root / 'build/hibr'))
env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(root / 'build/mods')))

def query(path, request="functions", name=None):
    p = subprocess.run([binary, '-c',
                        'need inspect; inspect "$1" "$2" -j', '_', request, str(path)],
                       env=env, capture_output=True, text=True, timeout=5)
    assert not p.stderr, p.stderr
    return json.loads(p.stdout)

with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'input.hibr'
    marker = Path(d) / 'marker'
    path.write_text(f'touch "{marker}"\nfn example(int value) {{\n ret "$value"\n}}\n')
    x = query(path)
    assert x['state'] == 'ok', x
    assert [f['name'] for f in x['functions']] == ['example']
    assert x['functions'][0]['params'][0]['type'] == 'int'
    assert query(path) == x
    plain = subprocess.run([binary, '-c', 'need inspect; inspect functions "$1"', '_', str(path)],
                           env=env, capture_output=True, text=True, timeout=5)
    assert plain.returncode == 0 and not plain.stderr
    assert 'example(int value)' in plain.stdout and 'functions (ok)' in plain.stdout
    assert not marker.exists()
    path.write_text('if then\n')
    assert query(path)['state'] == 'invalid'
    path.write_text('fn unfinished(')
    assert query(path)['state'] == 'incomplete'
    path.write_bytes(b'echo a\0echo b')
    assert query(path)['state'] == 'unreadable'
    path.write_text('')
    assert query(path)['state'] == 'empty'
print('preliminary inspection record tests passed')

with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'callbacks.hibr'
    path.write_text('fn tick() { ret ok; }\nfn start() { dt_ticker tick; tick; "${ap}_draw" 1; }\n')
    calls = query(path, 'calls')['calls']
    assert any(c['name'] == 'tick' and c['resolution'] == 'function' for c in calls)
    assert any(c['dynamic'] and c['resolution'] == 'unresolved' for c in calls)
    refs = query(path, 'refs')['refs']
    assert any(r['name'] == 'tick' and r['owner'] == 'start' for r in refs)
    assert all('source' in r and 'span' in r and r['known'] == 'syntax' for r in calls + refs)
    path.write_text('fn broken( {\n')
    result = query(path, 'check')
    assert result['diagnostics'] and result['state'] in ('invalid', 'incomplete')
print('call, reference and parse diagnostic tests passed')

with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'source.hibr'
    marker = Path(d) / 'must-not-exist'
    path.write_text(f'touch "{marker}"\nx=$(touch "{marker}")\nsource "{marker}"\nalias evil="touch {marker}"\nevil\nemail accounts\nfn valid() {{ ret ok; }}\n')
    assert query(path)['state'] == 'ok'
    assert not marker.exists()
    path.write_text('fn first() { ret ok; }\nif then\nfn last() { ret ok; }\n')
    result = query(path)
    assert result['state'] == 'invalid' and result['partial']
    assert [r['name'] for r in result['functions']] == ['first', 'last']
    path.write_text('"${ap}_draw" "private-argument"\n')
    result = query(path, 'calls')
    assert result['calls'][0]['expression'] == '"${ap}_draw"'
    assert 'private-argument' not in json.dumps(result)
    path.write_text('fn calc_draw(int id, int h, int w, int row, int col) { ret ok; }\ndt_app calc "private-title"\n')
    proc = subprocess.run([binary, '-c', 'need inspect; inspect calls "$1" --rules "$2" -j', '_', str(path), str(root / 'programs/desktop/conventions.json')], env=env, capture_output=True, text=True, timeout=5)
    assert not proc.stderr, proc.stderr
    result = json.loads(proc.stdout)
    assert any(r['name'] == 'calc_draw' and r['known'] == 'inferred' and r['rule'] == 'window.draw' for r in result['calls'])
    assert 'private-title' not in json.dumps(result)
    path.write_bytes(b'#' + b'a' * 4194304)
    assert query(path)['state'] == 'unreadable'
    module = Path(d) / 'state.so'
    subprocess.run([str(root / 'tools/tccld'), '-I' + str(root / 'include'), '-shared', '-fPIC', '-o', str(module), str(root / 'tests/inspect_state.c')], check=True, cwd=root)
    for source in ['need email; email accounts\nfn valid() { ret ok; }\n', 'if then\n', 'fn unfinished(']:
        path.write_text(source)
        proc = subprocess.run([binary, '-c', 'mod load "$1"; r := ixstate "$2"; echo "$r"', '_', str(module), str(path)], env=env, capture_output=True, text=True, timeout=5)
        assert proc.returncode == 0 and proc.stdout.strip() == 'preserved' and not proc.stderr, proc
print('non-execution, privacy, rules, limits, recovery and caller-state tests passed')

with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'external.hibr'
    path.write_text('fn local_fn() { :; }\nfn start() { top := external_call; dt_ticker external_callback; echo "external_call other"; }\n')
    command = 'need inspect; inspect refs "$1" --symbols "$2" -j'
    proc = subprocess.run([binary, '-c', command, '_', str(path), json.dumps(['external_callback', 'external_call'])],
                          env=env, capture_output=True, text=True, timeout=5)
    assert proc.returncode == 0 and not proc.stderr, proc
    refs = json.loads(proc.stdout)['refs']
    assert [r['name'] for r in refs] == ['external_callback'], refs
    assert refs[0]['resolution'] == 'unresolved' and refs[0]['lookup'] == 'supplied-symbol'
    assert not query(path, 'refs')['refs'], 'external candidate names must be explicitly supplied'
    calls = query(path, 'calls')['calls']
    bound = next(r for r in calls if r['name'] == 'external_call')
    assert bound['bound'] and bound['binding'] == 'top'
    plain = subprocess.run([binary, '-c', 'need inspect; inspect calls "$1"', '_', str(path)],
                           env=env, capture_output=True, text=True, timeout=5)
    assert plain.returncode == 0 and not plain.stderr, plain
    assert 'external_call unresolved bound (top :=)' in plain.stdout
    assert 'resolution: "unresolved"' in plain.stdout
    assert 'binding: "top"' in plain.stdout and 'partial: false' in plain.stdout
print('external reference candidates and lossless plain rendering passed')
