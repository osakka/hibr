#!/usr/bin/env python3
"""The md module against the CommonMark spec and GitHub's extensions.

tests/md/commonmark.json is the CommonMark 0.31.2 spec's examples, run with
the extensions off (md html -c), as cmark-gfm runs them. tests/md/gfm.json
is the extension examples from the GFM 0.29 spec -- tables, strikethrough,
extended autolinks, task lists, the tag filter -- run with them on. Both
specs are CC-BY-SA 4.0, by John MacFarlane and GitHub respectively.

Output is compared byte for byte, not normalised. Usage:
    tests/md_spec.py [-v] [--section NAME] [--example N] [--shell PATH]
"""
import argparse, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(shell, md, gfm):
    mods = os.environ.get('HIBR_TESTMODS') or ROOT + '/build/mods'
    so = os.environ.get('MD_SO', mods + '/md.so')
    cmd = 'mod load %s; md html%s' % (so, '' if gfm else ' -c')
    p = subprocess.run([shell, '-c', cmd], input=md.encode(), capture_output=True,
                       timeout=10)
    return p.stdout.decode(errors='replace'), p.returncode, p.stderr.decode(errors='replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--section')
    ap.add_argument('--example', type=int)
    ap.add_argument('--shell', default=os.environ.get('HIBR') or
                    os.path.join(ROOT, 'build', 'hibr'))
    a = ap.parse_args()
    suites = [('CommonMark 0.31.2', 'commonmark.json', False),
              ('GFM extensions', 'gfm.json', True)]
    total = bad = 0
    for title, f, gfm in suites:
        ex = json.load(open(os.path.join(HERE, 'md', f)))
        if a.section:
            ex = [e for e in ex if e['section'] == a.section]
        if a.example:
            ex = [e for e in ex if e['example'] == a.example]
        if not ex:
            continue
        with ThreadPoolExecutor(8) as pool:
            got = list(pool.map(lambda e: run(a.shell, e['markdown'], gfm), ex))
        fails = {}
        for e, (out, rc, err) in zip(ex, got):
            ok = rc == 0 and out == e['html'] and not err
            if not ok:
                fails.setdefault(e['section'], []).append(e['example'])
                if a.v:
                    print('--- %s example %d (%s)' % (title, e['example'], e['section']))
                    print('markdown:', repr(e['markdown']))
                    print('expected:', repr(e['html']))
                    print('got:     ', repr(out), err.strip())
        n = len(ex)
        nb = sum(len(v) for v in fails.values())
        total += n
        bad += nb
        print('%s: %d of %d pass' % (title, n - nb, n))
        for s, v in sorted(fails.items(), key=lambda x: -len(x[1])):
            print('  %-40s %3d  %s' % (s, len(v), ' '.join(map(str, v[:12]))))
    print('%d passed, %d failed' % (total - bad, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
