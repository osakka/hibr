"""Every variable fact inspect vars gives for tests/inspect_vars.hibr, row by row.

Each row is (line, owner, access, form, name, declared, known, exact, the bytes
its span covers, keys as (quote, text), subscripts, lexical, other fields).
A form whose handling is dropped loses its rows and fails here, and the forms
seen must be exactly the documented ones."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
binary = os.environ.get('HIBR', str(root / 'build/hibr'))
env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(root / 'build/mods')))
fixture = root / 'tests/inspect_vars.hibr'
FORMS = {'assign', 'append', 'prefix', 'array', 'array-append', 'array-key', 'bind',
         'local', 'declare', 'typeset', 'export', 'readonly', 'nameref', 'param', 'unset', 'read',
         'read-array', 'mapfile', 'printf', 'getopts', 'arr', 'json', 'str', 'match',
         'rsub', 'test', 'for', 'select', 'fd-variable', 'default-assign', 'expand',
         'length', 'keys', 'indirect', 'names', 'arithmetic', 'argument'}
EXPECTED = [
    (3, 'fx_draw', 'write', 'param', 'id', 'param', 'syntax', True, 'id', (), 0, False, {}),
    (3, 'fx_draw', 'write', 'param', 'h', 'param', 'syntax', True, 'h', (), 0, False, {}),
    (4, 'fx_draw', 'write', 'local', 'a', 'local', 'syntax', True, 'a', (), 0, False, {}),
    (4, 'fx_draw', 'read', 'expand', 'DT_FOCUS', None, 'syntax', True, '${DT_FOCUS}', (), 0, False, {}),
    (4, 'fx_draw', 'write', 'local', 'b', 'local', 'syntax', True, 'b', (), 0, False, {}),
    (4, 'fx_draw', 'read', 'expand', 'DT_ROWS', None, 'syntax', True, '$DT_ROWS', (), 0, False, {}),
    (5, 'fx_draw', 'write', 'local', 'DT_SHADOW', 'local', 'syntax', True, 'DT_SHADOW', (), 0, False, {}),
    (6, 'fx_draw', 'write', 'assign', 'DT_CLIP', None, 'syntax', True, 'DT_CLIP', (), 0, False, {}),
    (7, 'fx_draw', 'write', 'assign', 'DT', None, 'syntax', True, 'DT["$id"]["row"]', (('dynamic', '"$id"'), ('quoted', 'row')), 2, False, {}),
    (7, 'fx_draw', 'read', 'expand', 'id', 'param', 'syntax', True, '$id', (), 0, False, {}),
    (8, 'fx_draw', 'write', 'unset', 'DT_NOTEWIN', None, 'syntax', True, 'DT_NOTEWIN', (), 0, False, {}),
    (8, 'fx_draw', 'write', 'unset', 'DT_MAP', None, 'syntax', True, 'DT_MAP["gone"]', (('quoted', 'gone'),), 1, False, {}),
    (10, 'fx_draw', 'write', 'read-array', 'DT_LIST', None, 'syntax', True, 'DT_LIST', (), 0, False, {}),
    (10, 'fx_draw', 'write', 'read', 'DT_WHY', None, 'syntax', True, 'DT_WHY', (), 0, False, {}),
    (11, 'fx_draw', 'write', 'bind', 'r', None, 'syntax', True, 'r', (), 0, False, {}),
    (12, 'fx_draw', 'write', 'bind', 'DT_KB', None, 'syntax', True, 'DT_KB["k"]', (('quoted', 'k'),), 1, False, {}),
    (13, 'fx_draw', 'write', 'printf', 'DT_IMGARG', None, 'syntax', True, 'DT_IMGARG', (), 0, False, {}),
    (14, 'fx_draw', 'read', 'arithmetic', 'DT_COLS', None, 'inferred', True, 'DT_COLS', (), 0, True, {'rule': 'arithmetic-text'}),
    (14, 'fx_draw', 'write', 'arithmetic', 'DT_N', None, 'inferred', True, 'DT_N', (), 0, True, {'rule': 'arithmetic-text'}),
    (15, 'fx_draw', 'read', 'expand', 'DT_APPS', None, 'syntax', True, '${DT_APPS[$id]["title"]}', (('dynamic', '$id'), ('quoted', 'title')), 2, False, {}),
    (15, 'fx_draw', 'read', 'expand', 'id', 'param', 'syntax', True, '$id', (), 0, False, {}),
    (15, 'fx_draw', 'read', 'length', 'DT_LEN', None, 'syntax', True, '${#DT_LEN}', (), 0, False, {}),
    (15, 'fx_draw', 'read', 'keys', 'DT_KEYS', None, 'syntax', True, '${!DT_KEYS[@]}', (('unquoted', '@'),), 1, False, {}),
    (15, 'fx_draw', 'read', 'indirect', 'DT_IND', None, 'syntax', True, '${!DT_IND}', (), 0, False, {}),
    (15, 'fx_draw', 'read', 'expand', 'DT_DEF', None, 'syntax', True, '${DT_DEF:=x}', (), 0, False, {}),
    (15, 'fx_draw', 'write', 'default-assign', 'DT_DEF', None, 'syntax', True, '${DT_DEF:=x}', (), 0, False, {}),
    (16, 'fx_draw', 'write', 'nameref', 'ref', 'local', 'syntax', True, 'ref', (), 0, False, {}),
    (16, 'fx_draw', 'name', 'nameref', 'DT_CH', None, 'syntax', True, 'DT_CH', (), 0, False, {}),
    (17, 'fx_draw', 'write', 'declare', 'DT_GLOBAL', None, 'syntax', True, 'DT_GLOBAL', (), 0, False, {}),
    (18, 'fx_draw', 'write', 'mapfile', 'DT_LINES', None, 'syntax', True, 'DT_LINES', (), 0, False, {}),
    (19, 'fx_draw', 'write', 'json', 'DT_CONF', None, 'syntax', True, 'DT_CONF', (), 0, False, {'verb': 'parse'}),
    (20, 'fx_draw', 'read', 'arr', 'DT_QUEUE', None, 'syntax', True, 'DT_QUEUE', (), 0, False, {'verb': 'push'}),
    (20, 'fx_draw', 'write', 'arr', 'DT_QUEUE', None, 'syntax', True, 'DT_QUEUE', (), 0, False, {'verb': 'push'}),
    (21, 'fx_draw', 'write', 'assign', 'x', None, 'syntax', True, 'x', (), 0, False, {}),
    (21, 'fx_draw', 'read', 'expand', 'DT_INSUB', None, 'syntax', True, '$DT_INSUB', (), 0, False, {'scope': 'substitution'}),
    (21, 'fx_draw', 'write', 'assign', 'DT_SUBW', None, 'syntax', True, 'DT_SUBW', (), 0, False, {'scope': 'substitution'}),
    (22, 'fx_draw', 'read', 'expand', 'DT_DQ', None, 'syntax', True, '$DT_DQ', (), 0, False, {}),
    (23, 'fx_draw', 'read', 'test', 'DT_ISSET', None, 'syntax', True, 'DT_ISSET', (), 0, False, {}),
    (24, 'fx_draw', 'write', 'for', 'DT_LOOP', None, 'syntax', True, 'DT_LOOP', (), 0, False, {}),
    (25, 'fx_draw', 'write', 'fd-variable', 'DT_FD', None, 'syntax', True, 'DT_FD', (), 0, False, {}),
    (26, 'fx_draw', 'write', 'array', 'DT_ARR', None, 'syntax', True, 'DT_ARR', (), 0, False, {}),
    (26, 'fx_draw', 'write', 'array-key', 'DT_ARR', None, 'syntax', True, '[k1]', (('unquoted', 'k1'),), 1, False, {}),
    (26, 'fx_draw', 'write', 'array-key', 'DT_ARR', None, 'syntax', True, '[k2]', (('unquoted', 'k2'),), 1, False, {}),
    (27, 'fx_draw', 'read', 'expand', 'DT_HERE', None, 'syntax', False, 'cat <<-Q', (), 0, False, {}),
    (34, 'fx_draw', 'write', 'arithmetic', 'DT_M', None, 'inferred', True, 'DT_M["mk"]', (('quoted', 'mk'),), 1, True, {'rule': 'arithmetic-text'}),
    (35, 'fx_draw', 'write', 'assign', 'x', None, 'syntax', True, 'x', (), 0, False, {}),
    (35, 'fx_draw', 'read', 'arithmetic', 'DT_P', None, 'inferred', True, 'DT_P[$id]', (('dynamic', '$id'),), 1, True, {'rule': 'arithmetic-text'}),
    (35, 'fx_draw', 'read', 'expand', 'id', 'param', 'syntax', True, '$id', (), 0, False, {}),
    (37, '', 'write', 'assign', 'DT_TOP', None, 'syntax', True, 'DT_TOP', (), 0, False, {}),
    (38, '', 'write', 'declare', 'DT_TOPMAP', None, 'syntax', True, 'DT_TOPMAP', (), 0, False, {}),
    (39, '', 'write', 'append', 'DT_APPEND', None, 'syntax', True, 'DT_APPEND', (), 0, False, {}),
    (40, '', 'write', 'array-append', 'DT_LIST2', None, 'syntax', True, 'DT_LIST2', (), 0, False, {}),
    (41, '', 'write', 'prefix', 'DT_PRE', None, 'syntax', True, 'DT_PRE', (), 0, False, {}),
    (42, '', 'write', 'getopts', 'DT_OPT', None, 'syntax', True, 'DT_OPT', (), 0, False, {}),
    (43, '', 'write', 'arithmetic', 'DT_LET', None, 'inferred', True, 'DT_LET', (), 0, True, {'rule': 'arithmetic-text'}),
    (43, '', 'read', 'arithmetic', 'DT_LEFT', None, 'inferred', True, 'DT_LEFT', (), 0, True, {'rule': 'arithmetic-text'}),
    (44, '', 'read', 'expand', 'DT_SUBJ', None, 'syntax', True, '$DT_SUBJ', (), 0, False, {}),
    (44, '', 'write', 'match', 'DT_MATCHED', None, 'syntax', True, 'DT_MATCHED', (), 0, False, {}),
    (45, '', 'write', 'rsub', 'DT_RSUBBED', None, 'syntax', True, 'DT_RSUBBED', (), 0, False, {}),
    (46, '', 'read', 'json', 'DT_JSET', None, 'syntax', True, 'DT_JSET', (), 0, False, {'verb': 'set'}),
    (46, '', 'write', 'json', 'DT_JSET', None, 'syntax', True, 'DT_JSET', (), 0, False, {'verb': 'set'}),
    (47, '', 'read', 'json', 'DT_JGET', None, 'syntax', True, 'DT_JGET', (), 0, False, {'verb': 'get'}),
    (47, '', 'write', 'json', 'DT_JOUT', None, 'syntax', True, 'DT_JOUT', (), 0, False, {'verb': 'get'}),
    (48, '', 'read', 'arr', 'DT_ALEN', None, 'syntax', True, 'DT_ALEN', (), 0, False, {'verb': 'len'}),
    (48, '', 'write', 'arr', 'DT_ACOUNT', None, 'syntax', True, 'DT_ACOUNT', (), 0, False, {'verb': 'len'}),
    (49, '', 'write', 'str', 'DT_UP', None, 'syntax', True, 'DT_UP', (), 0, False, {'verb': 'upper'}),
    (50, '', 'read', 'test', 'DT_TESTED', None, 'syntax', True, 'DT_TESTED', (), 0, False, {}),
    (51, '', 'write', 'select', 'DT_PICK', None, 'syntax', True, 'DT_PICK', (), 0, False, {}),
    (52, '', 'write', 'arithmetic', 'DT_I', None, 'inferred', True, 'DT_I', (), 0, True, {'rule': 'arithmetic-text'}),
    (52, '', 'read', 'arithmetic', 'DT_I', None, 'inferred', True, 'DT_I', (), 0, True, {'rule': 'arithmetic-text'}),
    (52, '', 'read', 'arithmetic', 'DT_LIMIT', None, 'inferred', True, 'DT_LIMIT', (), 0, True, {'rule': 'arithmetic-text'}),
    (52, '', 'write', 'arithmetic', 'DT_I', None, 'inferred', True, 'DT_I', (), 0, True, {'rule': 'arithmetic-text'}),
    (53, '', 'write', 'read-array', 'DT_INLINE', None, 'syntax', True, 'DT_INLINE', (), 0, False, {}),
    (54, '', 'write', 'printf', 'DT_PINLINE', None, 'syntax', True, 'DT_PINLINE', (), 0, False, {}),
    (55, '', 'write', 'mapfile', 'DT_RA', None, 'syntax', True, 'DT_RA', (), 0, False, {}),
    (56, '', 'read', 'declare', 'DT_SHOWN', None, 'syntax', True, 'DT_SHOWN', (), 0, False, {}),
    (57, '', 'write', 'export', 'DT_EXPORTED', None, 'syntax', True, 'DT_EXPORTED', (), 0, False, {}),
    (58, '', 'write', 'readonly', 'DT_FROZEN', None, 'syntax', True, 'DT_FROZEN', (), 0, False, {}),
    (59, '', 'read', 'expand', 'DT_S', None, 'syntax', True, '${DT_S:DT_OFF:2}', (), 0, False, {}),
    (59, '', 'read', 'arithmetic', 'DT_OFF', None, 'inferred', True, 'DT_OFF', (), 0, True, {'rule': 'arithmetic-text'}),
    (59, '', 'read', 'arithmetic', 'DT_OLD', None, 'inferred', True, 'DT_OLD', (), 0, True, {'rule': 'arithmetic-text'}),
    (59, '', 'read', 'names', 'DT_PFX', None, 'syntax', True, '${!DT_PFX@}', (), 0, False, {}),
    (59, '', 'read', 'expand', 'DT_UNSET', None, 'syntax', True, '${DT_UNSET:-$DT_FALLBACK}', (), 0, False, {}),
    (59, '', 'read', 'expand', 'DT_FALLBACK', None, 'syntax', True, '$DT_FALLBACK', (), 0, False, {}),
    (60, '', 'read', 'expand', 'DT_DEEP', None, 'syntax', True, '$DT_DEEP', (), 0, False, {'scope': 'substitution'}),
    (60, '', 'read', 'expand', 'DT_PS', None, 'syntax', True, '$DT_PS', (), 0, False, {'scope': 'substitution'}),
    (61, '', 'read', 'expand', 'DT_RE', None, 'syntax', True, '$DT_RE', (), 0, False, {}),
    (62, '', 'read', 'expand', 'DT_CASE', None, 'syntax', True, '$DT_CASE', (), 0, False, {}),
    (63, '', 'write', 'declare', 'DT_TYPED', None, 'syntax', True, 'DT_TYPED', (), 0, False, {}),
    (64, 'fx_set', 'write', 'param', 'name', 'param', 'syntax', True, 'name', (), 0, False, {}),
    (64, 'fx_set', 'write', 'param', 'value', 'param', 'syntax', True, 'value', (), 0, False, {}),
    (65, 'fx_set', 'write', 'nameref', 'out', 'local', 'syntax', True, 'out', (), 0, False, {}),
    (66, 'fx_set', 'write', 'assign', 'out', 'local', 'syntax', True, 'out', (), 0, False, {}),
    (68, '', 'name', 'argument', 'DT_BYNAME', None, 'inferred', True, 'DT_BYNAME', (), 0, False, {'callee': 'fx_set', 'rule': 'by-name-parameter'}),
    (69, 'fx_more', 'write', 'param', 'x', 'param', 'syntax', True, 'x=$DT_PDEF', (), 0, False, {}),
    (69, 'fx_more', 'read', 'expand', 'DT_PDEF', None, 'syntax', True, '$DT_PDEF', (), 0, False, {}),
    (70, 'fx_more', 'write', 'local', 'items', 'local', 'syntax', True, 'items', (), 0, False, {}),
    (71, 'fx_more', 'write', 'local', 'y', 'local', 'syntax', True, 'y', (), 0, False, {}),
    (72, 'fx_more', 'write', 'assign', 'y', 'local', 'syntax', True, 'y', (), 0, False, {}),
    (72, 'fx_more', 'read', 'expand', 'x', 'param', 'syntax', True, '$x', (), 0, False, {}),
    (73, 'fx_more', 'write', 'assign', 'DT_SEEN', None, 'syntax', True, 'DT_SEEN', (), 0, False, {}),
    (73, 'fx_more', 'read', 'expand', 'items', 'local', 'syntax', True, '${items[0]}', (('unquoted', '0'),), 1, False, {}),
    (75, 'fx_get', 'write', 'param', 'name', 'param', 'syntax', True, 'name', (), 0, False, {}),
    (75, 'fx_get', 'read', 'indirect', 'name', 'param', 'syntax', True, '${!name}', (), 0, False, {}),
    (76, '', 'name', 'argument', 'DT_BYGET', None, 'inferred', True, 'DT_BYGET', (), 0, False, {'callee': 'fx_get', 'rule': 'by-name-parameter'}),
    (77, '', 'write', 'bind', 'DT_BOUND', None, 'syntax', True, 'DT_BOUND', (), 0, False, {}),
    (77, '', 'write', 'prefix', 'DT_ENV', None, 'syntax', True, 'DT_ENV', (), 0, False, {}),
    (78, '', 'write', 'typeset', 'DT_TS', None, 'syntax', True, 'DT_TS', (), 0, False, {}),
    (79, 'fx_put', 'write', 'param', 'name', 'param', 'syntax', True, 'name', (), 0, False, {}),
    (79, 'fx_put', 'read', 'expand', 'name', 'param', 'syntax', True, '$name', (), 0, False, {}),
    (80, '', 'name', 'argument', 'DT_BYPUT', None, 'inferred', True, 'DT_BYPUT', (), 0, False, {'callee': 'fx_put', 'rule': 'by-name-parameter'}),
    (81, 'fx_rest', 'write', 'param', 'id', 'param', 'syntax', True, 'id', (), 0, False, {}),
    (81, 'fx_rest', 'write', 'param', 'DT_RESTS', 'param', 'syntax', True, '...DT_RESTS', (), 0, False, {}),
    (82, 'fx_rest', 'write', 'local', 'DT_LA', 'local', 'syntax', True, 'DT_LA', (), 0, False, {}),
    (82, 'fx_rest', 'write', 'local', 'map', 'local', 'syntax', True, 'map', (), 0, False, {}),
    (82, 'fx_rest', 'write', 'local', 'DT_LB', 'local', 'syntax', True, 'DT_LB', (), 0, False, {}),
    (83, 'fx_rest', 'write', 'assign', 'DT_RESTS', 'param', 'syntax', True, 'DT_RESTS[0]', (('unquoted', '0'),), 1, False, {}),
    (84, 'fx_rest', 'write', 'assign', 'map', 'local', 'syntax', True, 'map[k]', (('unquoted', 'k'),), 1, False, {}),
    (85, 'fx_rest', 'read', 'fd-variable', 'DT_FD', None, 'syntax', True, 'DT_FD', (), 0, False, {}),
]

checks = 0


def check(ok, what):
    global checks
    checks += 1
    if not ok:
        raise SystemExit('FAIL ' + what)


def query(path, *extra):
    p = subprocess.run([binary, '-c', 'need inspect; inspect vars "$@"', '_', str(path), *extra, '-j'],
                       env=env, capture_output=True, text=True, timeout=10)
    check(not p.stderr, 'no stderr: ' + p.stderr)
    return p.returncode, json.loads(p.stdout)


def rows(result, src):
    out = []
    for v in result['vars']:
        span = v['span']
        text = src[span['start']:span['end']].decode()
        line = src[:span['start']].count(b'\n') + 1
        check(line == span['line'], f'span line {span["line"]} is the line its start is on, {line}')
        check(v['source']['id'] == result['source']['id'], 'every fact carries the inspected identity')
        keys = tuple((k['quote'], k['text']) for k in v['keys'])
        extra = {k: v[k] for k in ('verb', 'callee', 'rule', 'lookup', 'scope') if k in v}
        out.append((span['line'], v['owner'], v['access'], v['form'], v['name'], v['declared'], v['known'],
                    v['exact'], text, keys, v['subscripts'], v['lexical'], extra))
    return out


src = fixture.read_bytes()
status, result = query(fixture)
check(status == 0 and result['state'] == 'ok' and not result['partial'], f'fixture inspects ok: {result["state"]}')
check(result['inspect'] == 1, 'interface version 1')
got = rows(result, src)
for i, (want, have) in enumerate(zip(EXPECTED, got)):
    check(want == have, f'row {i}: wanted {want!r}, got {have!r}')
check(len(got) == len(EXPECTED), f'{len(EXPECTED)} rows wanted, {len(got)} given: {got[len(EXPECTED):] or EXPECTED[len(got):]}')
forms = {r[3] for r in got}
check(forms == FORMS, f'forms seen {sorted(forms - FORMS)} beyond, {sorted(FORMS - forms)} missing')
for r in got:
    check(r[7] or r[8] == 'cat <<-Q', f'only the here-document body is inexact: {r!r}')
    check(r[11] == (r[3] == 'arithmetic') and (r[6] == 'inferred') == (r[11] or 'rule' in r[12]),
          f'lexical facts and only those, or a rule, are inferred: {r!r}')
text = json.dumps(result)
for private in ('DT_COMMENT', 'DT_SINGLE', 'DT_DOUBLE', 'DT_QHERE', 'DT_KEPT', 'fx_gone'):
    check(private not in text, f'{private} is text or a function, never a variable fact')
print(f'{len(got)} fixture rows across {len(forms)} forms matched')

status, result = query(fixture, '--symbols', json.dumps(['DT_KEPT', 'DT_SINGLE', 'DT_DOUBLE', 'DT_CLIP']))
named = [(v['name'], v['callee'], v['lookup'], v['known']) for v in result['vars'] if v.get('lookup')]
check(named == [('DT_SINGLE', 'echo', 'supplied-symbol', 'syntax'), ('DT_KEPT', 'dt_keep', 'supplied-symbol', 'syntax')],
      f'supplied names are matched in literal arguments only: {named}')
check(len(result['vars']) == len(EXPECTED) + 2, 'supplied names add rows and take none away')
status, result = query(fixture, '--symbols', '[42]')
check(status != 0 and result['state'] == 'unreadable' and result['vars'] == [], 'bad symbols are refused')
status, result = query(fixture, 'fx_set')
check({v['owner'] for v in result['vars']} == {'fx_set'} and len(result['vars']) == 4, 'a function filter keeps its own')

with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'source.hibr'
    marker = Path(d) / 'marker'
    path.write_text(f'X=$(touch "{marker}")\n(( Y = $(touch "{marker}") ))\nread Z < <(touch "{marker}")\n')
    status, result = query(path)
    check(not marker.exists(), 'nothing inspected runs')
    check([(v['name'], v['access']) for v in result['vars']] == [('X', 'write'), ('Y', 'write'), ('Z', 'write')],
          f'writes beside substitutions: {result["vars"]}')
    path.write_text('x=`echo \\$secret`\nfn f() { local -n r=$2; }\nf a B\n')
    status, result = query(path)
    check(result['partial'], 'a transformed substitution is partial')
    check([(v['name'], v['form']) for v in result['vars']][-1] == ('B', 'argument'), 'the second argument is taken by name')
    path.write_text('echo "${a[$(( $(s1) ))]}" "${b[`s2`]}"\nc[$(( $(s3) ))]=1\n')
    status, result = query(path)
    check([(v['name'], v['keys'][0]['text']) for v in result['vars']] == [('a', None), ('b', None), ('c', None)],
          f'a subscript holding command code anywhere gives no text: {result["vars"]}')
    path.write_text('fn broken( {\nA=1\n')
    status, result = query(path)
    check(status != 0 and result['state'] in ('invalid', 'incomplete'), 'damaged source is not ok')
    plain = subprocess.run([binary, '-c', 'need inspect; inspect vars "$1"', '_', str(fixture)],
                           env=env, capture_output=True, text=True, timeout=10)
    check(plain.returncode == 0 and not plain.stderr, 'plain rendering')
    check('DT_CLIP write assign in fx_draw [syntax]' in plain.stdout and 'vars (ok)' in plain.stdout,
          'plain rendering names the access and form')
print(f'{checks} checks; inspect vars passed')
