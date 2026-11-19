#!/usr/bin/env python3
"""Check strict functions using Hibr's variable inspection records.

The former source-text checker remains a temporary parity oracle in
strictvars_legacy.py. It is not used to produce the new checker's findings.
"""
import os
from inspectlib import records
from strictvars_legacy import SHELL, assigned, declared, functions, read_all
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(os.path.dirname(HERE), 'programs', 'desktop')
WRITES = {'write', 'bind', 'loop-write', 'array-write'}


def inspected_files():
    for directory, _, names in os.walk(DESK):
        for name in sorted(names):
            if name.endswith('.hibr'):
                yield os.path.join(directory, name)


def findings_from_inspect(path, vars_records, tops):
    """Return per-function strict findings from parsed producer evidence."""
    source_functions = functions(path)
    rows = {}
    for record in vars_records:
        owner = record['owner']
        if owner:
            rows.setdefault(owner, []).append(record)
    found = []
    for function in source_functions:
        name = function['name']
        owned = rows.get(name, [])
        local = {r['name'] for r in owned if r['kind'] == 'parameter'}
        local.update(r['name'] for r in owned
                     if r['kind'] == 'declaration' and r.get('storage') == 'local')
        declaration_lines = {r['span']['line'] for r in owned
                             if r['kind'] == 'declaration'}
        assigned_names = {r['name'] for r in owned
                          if r['kind'] in WRITES and
                          r['span']['line'] not in declaration_lines}
        for variable in sorted(assigned_names - local - tops - SHELL):
            found.append((name, variable))
    return found


def legacy_answers(path, legacy_functions, legacy_tops, strict):
    """Return legacy per-function assignment sets for migration parity."""
    old = {}
    for file, name, params, body in legacy_functions:
        if file != path or file not in strict:
            continue
        local = set(params)
        declaration_lines = set()
        for line, statement in body:
            glob, names = declared(statement)
            if glob is False:
                local.update(names)
            if declared(statement)[0] is not None:
                declaration_lines.add(line)
        names = set()
        for line, statement in body:
            if line not in declaration_lines:
                names.update(assigned(statement))
        old[name] = names - local - legacy_tops - SHELL
    return old


def main():
    wanted = [os.path.join(DESK, arg) for arg in sys.argv[1:]]
    selected = lambda path: not wanted or any(path.startswith(p) for p in wanted)
    files = list(inspected_files())

    # Discover file-scope names first, because strict checks may refer to a
    # global declared in any other desktop source file.
    top_names = set()
    parsed = {}
    strict_paths = set()
    legacy_tops, legacy_functions, legacy_strict = read_all()
    for path in files:
        vars_records = records(path, 'vars')['vars']
        parsed[path] = vars_records
        if any(line == 'strict' or line.startswith('strict ') or
               line.startswith('strict\t')
               for line in open(path, encoding='utf-8').read().splitlines()):
            strict_paths.add(path)
        top_names.update(r['name'] for r in vars_records
                         if r['owner'] is None and r['kind'] in
                         {'write', 'bind', 'declaration', 'array-write'})

    new_findings = []
    parity_failures = []
    checked = 0
    for path in files:
        if path not in strict_paths or path not in legacy_strict or not selected(path):
            continue
        checked += 1
        current = findings_from_inspect(path, parsed[path], top_names)
        new_findings.extend((path, name, variable) for name, variable in current)

        old = legacy_answers(path, legacy_functions, legacy_tops, legacy_strict)
        new_by_function = {}
        for name, variable in current:
            new_by_function.setdefault(name, set()).add(variable)
        all_functions = set(old) | set(new_by_function)
        for name in sorted(all_functions):
            before = old.get(name, set())
            after = new_by_function.get(name, set())
            if before != after:
                parity_failures.append((path, name, before, after))

    for path, name, before, after in parity_failures:
        rel = os.path.relpath(path, DESK)
        print('PARITY %s: %s old=%s inspect=%s' %
              (rel, name, ','.join(sorted(before)) or '-',
               ','.join(sorted(after)) or '-'))
    for path, name, variable in new_findings:
        print('FAIL %s: %s in %s creates a global' %
              (os.path.relpath(path, DESK), variable, name))
    print('\n%d strict files checked, %d findings, %d parity differences' %
          (checked, len(new_findings), len(parity_failures)))
    sys.exit(1 if new_findings or parity_failures else 0)


if __name__ == '__main__':
    main()
