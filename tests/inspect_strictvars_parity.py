"""Audit strictvars.py's variable facts against its historical regex checker."""
import os
from pathlib import Path
import subprocess
import sys

from inspectlib import records

root = Path(__file__).resolve().parents[1]
baseline = sys.argv[1] if len(sys.argv) > 1 else '174fcb86'
source = subprocess.check_output(['git', 'show', f'{baseline}:tests/strictvars.py'], cwd=root, text=True)
old = {'__name__': '_strictvars_baseline', '__file__': str(root / 'tests/strictvars.py')}
exec(compile(source, '<historical strictvars checker>', 'exec'), old)
sys.path.insert(0, str(root / 'tests'))
import strictvars

desk = old['DESK']
tops_old, funcs, strict_old = old['read_all']()
before = set()
for path, name, params, body in funcs:
    if path not in strict_old:
        continue
    local = set(params)
    for _, text in body:
        glob, names = old['declared'](text)
        if glob is False:
            local |= names
    for line, text in body:
        if not old['DECL'].match(text):
            before |= {(os.path.relpath(path, desk), line, v) for v in old['assigned'](text) - local}
files, strict, _ = strictvars.read_all()
tops_new = {v['name'] for vs in files.values() for v in vs if not v['owner']}
after = {}
for path, writes in files.items():
    for v in writes:
        if path in strict and v['owner'] and v['form'] not in strictvars.DECL and not v['declared']:
            after[(os.path.relpath(path, desk), v['span']['line'], v['name'])] = v['form']
for label, difference in [('removed', before - set(after)), ('added', set(after) - before)]:
    for edge in sorted(difference):
        print(label, *edge, after.get(edge, ''))
for label, difference in [('top-level only before', tops_old - tops_new), ('top-level only after', tops_new - tops_old)]:
    print(label + ':', ' '.join(repr(n) for n in sorted(difference)))
print(f'{len(before)} old, {len(after)} new, {len(before & set(after))} unchanged written-and-not-local sites; '
      f'{len(tops_old)} old, {len(tops_new)} new top-level names')
if before != set(after) or tops_old != tops_new:
    print('Each difference is explained in mods/inspect/PARITY.md; re-explain any that is not.')
