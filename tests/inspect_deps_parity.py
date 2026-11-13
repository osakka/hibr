"""Audit current dependency evidence against the historical token checker."""
import ast
import os
from pathlib import Path
import subprocess
import sys

from inspectlib import functions, records

root = Path(__file__).resolve().parents[1]
baseline = sys.argv[1] if len(sys.argv) > 1 else 'ef6e412'
source = subprocess.check_output(['git', 'show', f'{baseline}:tests/deps.py'], cwd=root, text=True)
namespace = {'__name__': '_deps_baseline', '__file__': str(root / 'tests/deps.py')}
exec(compile(source, '<historical deps checker>', 'exec'), namespace)
main = next(n for n in ast.parse(source).body if isinstance(n, ast.FunctionDef) and n.name == 'main')
prefix = []
for statement in main.body:
    if isinstance(statement, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'fails' for t in statement.targets):
        break
    prefix.append(statement)
exec(compile(ast.Module(body=prefix, type_ignores=[]), '<historical deps setup>', 'exec'), namespace)
old, new = set(), set()
owner, units = namespace['owner'], namespace['unit']
for file in namespace['inst']:
    relative = os.path.relpath(file, namespace['DESK'])
    starts = {f['span']['line'] for f in functions(file)}
    for line, text in enumerate(Path(file).read_text().splitlines(), 1):
        if line in starts:
            continue
        for name in namespace['TOK'].findall(namespace['code'](text)):
            if name in owner and not owner[name] & units[file]:
                old.add((relative, line, name))
    for fact in records(file, 'calls')['calls'] + records(file, 'refs', symbols=owner)['refs']:
        name = fact.get('name')
        if fact['known'] == 'syntax' and name in owner and not owner[name] & units[file]:
            new.add((relative, fact['span']['line'], name))
for label, difference in [('removed', old - new), ('added', new - old)]:
    for edge in sorted(difference):
        print(label, *edge)
print(f'{len(old)} old, {len(new)} new, {len(old & new)} unchanged dependency candidates')
if old != new:
    sys.exit('Explain each difference in mods/inspect/PARITY.md before accepting this audit.')
