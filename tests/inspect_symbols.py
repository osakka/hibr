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
    return len(cases)


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    binary = os.environ.get('HIBR', str(root / 'build/hibr'))
    env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(root / 'build/mods')))
    print(f'{exercise(binary, env)} symbol limit cases passed')
