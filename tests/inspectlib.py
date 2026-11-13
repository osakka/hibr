"""Shared test consumer of Hibr inspection records; no source parser here."""
import functools
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def records(path, request='functions', rules=None):
    """Read facts from the producer, caching by exact input content identity."""
    path = str(Path(path).resolve())
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    rulepath = str(Path(rules).resolve()) if rules else ''
    ruleid = hashlib.sha256(Path(rulepath).read_bytes()).hexdigest() if rulepath else ''
    return _records(path, request, digest, rulepath, ruleid)


@functools.lru_cache(maxsize=1024)
def _records(path, request, digest, rules, ruleid):
    """Reject a changed source rather than attaching facts to earlier bytes."""
    binary = os.environ.get('HIBR', str(ROOT / 'build/hibr'))
    env = dict(os.environ, HIBR_MODPATH=os.environ.get('HIBR_TESTMODS', str(ROOT / 'build/mods')))
    args = [binary, '-c', 'need inspect; inspect "$1" "$2" -j', '_', request, path]
    if rules:
        args = [binary, '-c', 'need inspect; inspect "$1" "$2" --rules "$3" -j', '_', request, path, rules]
    proc = subprocess.run(args,
                          cwd=ROOT, env=env, capture_output=True, text=True, timeout=10)
    if proc.stderr or proc.returncode:
        raise RuntimeError(f'inspection failed for {path}: {proc.stderr or proc.stdout}')
    result = json.loads(proc.stdout)
    if result['state'] not in ('ok', 'empty'):
        raise RuntimeError(f'incomplete inspection for {path}: {result}')
    if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'source changed during inspection: {path}; retry')
    if rules and hashlib.sha256(Path(rules).read_bytes()).hexdigest() != ruleid:
        raise RuntimeError(f'conventions changed during inspection: {rules}; retry')
    return result


def functions(path):
    """Return the authoritative function records for a source file."""
    return records(path)['functions']
