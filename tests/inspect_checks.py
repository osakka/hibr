"""Structural desktop checks using Hibr records and shared convention data.

    python3 tests/inspect_checks.py MODE[,MODE...] FILE...

Each mode is one rule, run over the files given, and several given at once
share one query a file; `app` runs every rule an
app is held to (Gitea #541), so the same rules hold the shipped tree
(tests/540-examples.t) and a catalogue bundle before it is indexed. A
finding is printed and makes the exit status 1."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import os
import json
from pathlib import Path
import re
import sys

from inspectlib import ROOT, functions, records

CONVENTIONS = ROOT / 'programs/desktop/conventions.json'
SETTINGS = ROOT / 'programs/desktop/wm/settings.hibr'
ROLE = re.compile(r'^(DT_[A-Z0-9_]+)="\$\{DT_[A-Z0-9_]+-#', re.M)
PENVAR = re.compile(r'"?\$\{?(DT_[A-Z0-9_]+)\}?"?')
SAYS = re.compile(rb'dt_app |cp_pane |dt_item |dt_dim |dt_sub |dt_mark |dt_menu |dt_note|dt_confirm |dbase_say ')
GLYPHSET = re.compile(rb'GLYPHS=\(|DT_GLYPH=|icon:-')
CALLARGS = ('console', 'dt_menu', 'strict')


def calls(path):
    """A file's calls, as every rule asks for them: one query a file."""
    return records(path, 'calls', CONVENTIONS, arguments=CALLARGS)['calls']


def asked(path, call, name):
    """Refuse a producer from before `calls --arguments` rather than pass."""
    if call['name'] == name and 'arguments' not in call:
        raise SystemExit(f'{path}: the producer gave no arguments for {name}; it is older than this check')
    return call['name'] == name


def duplicates(paths):
    """A function defined twice in one file silently replaces the first."""
    out = []
    for path in paths:
        for name, count in sorted(Counter(r['name'] for r in functions(path)).items()):
            if count > 1:
                out.append(f'defined twice in {path}: {name}')
    return out


def combined(paths):
    """The same, across files that are sourced into one shell together."""
    together = Counter(r['name'] for path in paths for r in functions(path))
    return [f"defined twice across the desktop's parts: {name}"
            for name, count in sorted(together.items()) if count > 1]


def arity(paths):
    """A callback names every argument the window manager hands it."""
    rules = json.loads(CONVENTIONS.read_text())['roles']
    prefixes = {role: set() for role in rules}
    out = []
    for path in paths:
        for call in calls(path):
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
                        out.append(f"{path}: {name} names {len(record['params'])} of the {rule['arity']} arguments it is called with")
                    break
    return out


def windows(paths, opens, whole):
    """No window's _open opens its own kind; every app's bar begins File, then Edit."""
    byfile = {path: calls(path) for path in paths}
    kinds = {call['registration']['prefix'] for found in byfile.values() for call in found
             if call.get('registration', {}).get('role') == 'window'
             and (opens or call['name'] == 'dt_app')}
    out, seen = [], 0
    for path in paths:
        for record in functions(path):
            prefix, _, suffix = record['name'].rpartition('_')
            if prefix not in kinds or suffix != ('open' if opens else 'menus'):
                continue
            mine = [c for c in byfile[path] if c['known'] == 'syntax' and c.get('owner') == record['name']]
            seen += 1
            if opens:
                for call in mine:
                    if call['name'] == 'dt_new' and call.get('registration', {}).get('prefix') == prefix:
                        out.append(f"{path}: {prefix}_open opens a {prefix} window, which calls {prefix}_open again")
                continue
            menus, edited = 0, False
            for call in mine:
                if call['name'] == 'dt_editmenu':
                    edited = True
                if not asked(path, call, 'dt_menu'):
                    continue
                menus += 1
                title = call['arguments'][0]['text'] if call['arguments'] else ''
                if menus == 1 and title != 'File':
                    out.append(f"{path}: {prefix}_menus opens with {title}, not File")
                if menus == 2 and not edited:
                    out.append(f"{path}: {prefix}_menus declares {title} before it calls dt_editmenu")
    if whole and paths and not seen:
        raise SystemExit(f'no window prefix has a _{"open" if opens else "menus"}: the check found nothing to check')
    return out


def strict(paths):
    """A file says `strict` at its top level, with every check on."""
    out = []
    for path in paths:
        if not any(c['known'] == 'syntax' and asked(path, c, 'strict') and not c['owner']
                   and not c.get('scope') and not c['arguments'] for c in calls(path)):
            out.append(f'not under every strict check: {path}')
    return out


def pens(paths):
    """A pen's two colours name a theme's colour or role, one settings.hibr
    gives a default; what follows them is attributes."""
    roles = set(ROLE.findall(SETTINGS.read_text()))
    out = []
    for path in paths:
        for call in calls(path):
            if not asked(path, call, 'console') or call['known'] != 'syntax':
                continue
            words = call['arguments']
            if not words or words[0]['text'] != 'pen':
                continue
            words = words[1:]
            if words and words[0]['text'] == '-p':
                words = words[2:]
            for word in words[:2]:
                m = PENVAR.fullmatch(word['text'] or '')
                if m and m.group(1) not in roles:
                    out.append(f"{path}:{call['span']['line']}: a pen uses {m.group(1)}, which no theme defines")
    return out


def glyphs(paths):
    """A glyph drawn is named in GL, never written; read as text, since what a
    string holds is the one thing inspect does not disclose. What stays
    literal: an app's or pane's icon, a menu's or a note's own words, and
    lists that are themselves a choice of characters."""
    out = []
    for path in paths:
        if path.endswith('wm/glyphs.hibr'):
            continue
        text = re.sub(rb'\\\n[ \t]*', b' ', Path(path).read_bytes())
        for line in text.split(b'\n'):
            if (line.lstrip().startswith(b'#') or SAYS.search(line) or GLYPHSET.search(line)
                    or (path.endswith('wm/bar.hibr') and b'ret "' in line)):
                continue
            if re.search(rb'[\x80-\xff]', line.replace('…'.encode(), b'').replace('—'.encode(), b'')):
                out.append(f"{path}: a glyph written, not named: {line.decode(errors='replace')}")
    return out


APP = (duplicates, arity, lambda p: windows(p, True, False), lambda p: windows(p, False, False),
       strict, pens, glyphs)
MODES = {'duplicates': duplicates, 'combined': combined, 'arity': arity,
         'opens': lambda p: windows(p, True, True), 'menus': lambda p: windows(p, False, True),
         'strict': strict, 'pens': pens, 'glyphs': glyphs,
         'app': lambda p: [f for rule in APP for f in rule(p)]}

mode, *paths = sys.argv[1:]
modes = mode.split(',')
if not all(m in MODES for m in modes):
    raise SystemExit('unknown structural check')
asks = [ask for ask, users in ((functions, {'duplicates', 'combined', 'arity', 'opens', 'menus', 'app'}),
                               (calls, {'arity', 'opens', 'menus', 'strict', 'pens', 'app'}))
        if users & set(modes)]
with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
    list(pool.map(lambda p: [ask(p) for ask in asks], paths))
found = [f for m in modes for f in MODES[m](paths)]
for finding in found:
    print(finding)
sys.exit(1 if found else 0)
