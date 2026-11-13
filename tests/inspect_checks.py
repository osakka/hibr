"""Structural desktop checks using Hibr records and shared convention data."""
from collections import Counter
import json
from pathlib import Path
import sys

from inspectlib import ROOT, functions, records

mode, *paths = sys.argv[1:]
if mode in ('duplicates', 'combined'):
    together = Counter()
    for path in paths:
        names = Counter(r['name'] for r in functions(path))
        together.update(names)
        if mode == 'duplicates':
            for name, count in sorted(names.items()):
                if count > 1:
                    print(f'defined twice in {path}: {name}')
    if mode == 'combined':
        for name, count in sorted(together.items()):
            if count > 1:
                print(f"defined twice across the desktop's parts: {name}")
elif mode == 'arity':
    conventions = ROOT / 'programs/desktop/conventions.json'
    rules = json.loads(conventions.read_text())['roles']
    prefixes = {role: set() for role in rules}
    for path in paths:
        for call in records(path, 'calls', conventions)['calls']:
            registration = call.get('registration')
            if registration:
                prefixes[registration['role']].add(registration['prefix'])
    for path in paths:
        for record in functions(path):
            if not record['declared_signature'] or any(a['rest'] for a in record['params']):
                continue
            name = record['name']
            prefix, _, suffix = name.rpartition('_')
            for role in rules:
                if prefix in prefixes[role]:
                    rule = rules[role]['callbacks'].get(suffix)
                    if rule and len(record['params']) < rule['arity']:
                        print(f"{path}: {name} names {len(record['params'])} of the {rule['arity']} arguments it is called with")
                    break
else:
    raise SystemExit('unknown structural check')
