"""Boundary cases shared by the inspection suite and its mutation fuzzer."""
import json
import os
from pathlib import Path
import subprocess
import tempfile


def exercise(binary, env):
    cases = [([], None), (['callback'] * 4096, None),
             (['callback'] * 4097, 'symbols exceeds 4096 names'),
             (['a' * 128], None), (['a' * 129], 'symbol exceeds 128 bytes'),
             ({'name': 'callback'}, 'symbols must be an array'),
             ([None], 'symbols must be an array'),
             (['not-an-identifier'], 'symbols must be an array'),
             ('a' * 1048576, 'inspection options exceed 1 MiB')]
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'source.hibr'
        options = Path(directory) / 'symbols.json'
        source.write_text('dt_ticker callback\n')
        for symbols, error in cases:
            options.write_text(json.dumps(symbols))
            command = 'need inspect; names=$(command cat "$2"); inspect refs "$1" --symbols "$names" -j'
            result = subprocess.run([binary, '-c', command, '_', str(source), str(options)],
                                    env=env, capture_output=True, text=True, timeout=10)
            assert not result.stderr, result.stderr
            body = json.loads(result.stdout)
            if error:
                assert result.returncode != 0 and body['state'] == 'unreadable', body
                assert error in body['diagnostics'][0]['message'], body
                assert body['refs'] == [], body
            else:
                assert result.returncode == 0 and body['state'] == 'ok', body
        argcases = [([], None), (['dt_menu'] * 4096, None),
                    (['dt_menu'] * 4097, 'arguments exceeds 4096 names'),
                    (['.'], None), (['a' * 128], None), (['a' * 129], 'argument command exceeds 128 bytes'),
                    ({'name': 'dt_menu'}, 'arguments must be an array'), ([None], 'arguments must be an array'),
                    ([''], 'arguments must be an array'), (['two words'], 'arguments must be an array')]
        for names, error in argcases:
            options.write_text(json.dumps(names))
            command = 'need inspect; names=$(command cat "$2"); inspect calls "$1" --arguments "$names" -j'
            result = subprocess.run([binary, '-c', command, '_', str(source), str(options)],
                                    env=env, capture_output=True, text=True, timeout=10)
            assert not result.stderr, result.stderr
            body = json.loads(result.stdout)
            if error:
                assert result.returncode != 0 and body['state'] == 'unreadable', body
                assert error in body['diagnostics'][0]['message'], body
                assert body['calls'] == [], body
            else:
                assert result.returncode == 0 and body['state'] == 'ok', body
        text = ('dt_menu "File" plain x"$y" $(secret) "a"b\n'
                'dt_menu $(( $(s1) )) "${x:-$(s2)}" "${a[$(s3)]}" `s4` <(s5) ${y//$(s6)/z}\n'
                'echo private words\n')
        source.write_text(text)
        options.write_text(json.dumps(['dt_menu']))
        command = 'need inspect; names=$(command cat "$2"); inspect calls "$1" --arguments "$names" -j'
        asked = json.loads(subprocess.run([binary, '-c', command, '_', str(source), str(options)],
                                          env=env, capture_output=True, text=True, timeout=10).stdout)
        plain = subprocess.run([binary, '-c', 'need inspect; inspect calls "$1" -j', '_', str(source)],
                               env=env, capture_output=True, text=True, timeout=10).stdout
        menu, nested = [c for c in asked['calls'] if c['name'] == 'dt_menu']
        assert [(a['text'], a['quote']) for a in nested['arguments']] == [(None, 'dynamic')] * 6, nested
        got = [(a['text'], a['quote'], text[a['span']['start']:a['span']['end']]) for a in menu['arguments']]
        assert got == [('File', 'quoted', '"File"'), ('plain', 'unquoted', 'plain'), ('x"$y"', 'dynamic', 'x"$y"'),
                       (None, 'dynamic', '$(secret)'), ('ab', 'unquoted', '"a"b')], got
        assert all('arguments' not in c for c in asked['calls'] if c['name'] != 'dt_menu'), asked
        assert 'private' not in json.dumps(asked) and 'arguments' not in plain and 'File' not in plain, plain
        argcases.append('arguments are given for the commands asked about, and only those')
    return len(cases) + len(argcases)


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    binary = os.environ.get('HIBR', str(root / 'build/hibr'))
    env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(root / 'build/mods')))
    print(f'{exercise(binary, env)} symbol limit cases passed')
