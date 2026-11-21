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
elif mode in ('opens', 'menus'):
    conventions = ROOT / 'programs/desktop/conventions.json'
    calls = {path: records(path, 'calls', conventions, arguments=('dt_menu',))['calls'] for path in paths}
    windows = {call['registration']['prefix'] for found in calls.values() for call in found
               if call.get('registration', {}).get('role') == 'window'
               and (mode == 'opens' or call['name'] == 'dt_app')}
    seen = 0
    for path in paths:
        for record in functions(path):
            prefix, _, suffix = record['name'].rpartition('_')
            if prefix not in windows or suffix != ('open' if mode == 'opens' else 'menus'):
                continue
            mine = [c for c in calls[path] if c['known'] == 'syntax' and c.get('owner') == record['name']]
            seen += 1
            if any(c['name'] == 'dt_menu' and 'arguments' not in c for c in mine):
                raise SystemExit(f'{path}: the producer gave no arguments for dt_menu; it is older than this check')
            if mode == 'opens':
                for call in mine:
                    if call['name'] == 'dt_new' and call.get('registration', {}).get('prefix') == prefix:
                        print(f"{path}: {prefix}_open opens a {prefix} window, which calls {prefix}_open again")
                continue
            menus, edited = 0, False
            for call in mine:
                if call['name'] == 'dt_editmenu':
                    edited = True
                if call['name'] != 'dt_menu':
                    continue
                menus += 1
                title = call['arguments'][0]['text'] if call['arguments'] else ''
                if menus == 1 and title != 'File':
                    print(f"{path}: {prefix}_menus opens with {title}, not File")
                if menus == 2 and not edited:
                    print(f"{path}: {prefix}_menus declares {title} before it calls dt_editmenu")
    if paths and not seen:
        raise SystemExit(f'no window prefix has a _{"open" if mode == "opens" else "menus"}: the check found nothing to check')
else:
    raise SystemExit('unknown structural check')
