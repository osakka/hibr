#!/usr/bin/env python3
# Mutation fuzzer for the parser: takes every test script, applies random
# byte-level mutations, and runs nsh -n on the result under a timeout.
# Any signal death, sanitizer report or hang is a finding.
import os, random, subprocess, sys, glob
nsh = sys.argv[1] if len(sys.argv) > 1 else './nsh'
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 500
seeds = [open(f,'rb').read() for f in glob.glob('tests/*.t')+glob.glob('tests/*.nsh')]
tokens = [b'$(', b'${', b'((', b'"', b"'", b'{', b'}', b'[', b']', b'|', b'&', b';', b'<<', b'<<<', b'>&', b'\\', b'\n', b'$', b'fn ', b'case ', b'esac', b'in ', b'do ', b'done', b'if ', b'fi', b'`', b'<(', b'{fd}>', b'&>', b';;', b';&', b'$\'', b'..', b',', b'**', b'((', b'))', b'[[', b']]', b'=~', b'+=', b'++', b'for ((', b'select ', b'!!', b'!$', b'?', b':']
random.seed(int(os.environ.get('SEED','1')))
def mutate(b):
    b = bytearray(b)
    for _ in range(random.randint(1,8)):
        op = random.random()
        if not b: b = bytearray(b'x')
        i = random.randrange(len(b))
        if op < 0.3: b[i] = random.randrange(256)
        elif op < 0.5: del b[i:i+random.randint(1,16)]
        elif op < 0.8: b[i:i] = random.choice(tokens)
        else: b[i:i] = b[random.randrange(len(b)):][:random.randint(1,40)]
    return bytes(b)
found = 0; hangs = 0
for n in range(rounds):
    data = mutate(random.choice(seeds))
    path = '/tmp/nsh-fuzz-%d.t' % n
    open(path,'wb').write(data)
    try:
        r = subprocess.run([nsh,'-n',path], capture_output=True, timeout=3)
        crash = r.returncode < 0 or r.returncode >= 128 or b'AddressSanitizer' in r.stderr or b'runtime error' in r.stderr
        if crash:
            found += 1
            keep = 'tests/fuzz-crash-%d.t' % found
            os.rename(path, keep)
            print('CRASH rc=%d saved %s' % (r.returncode, keep)); print(r.stderr.decode(errors='replace')[:600])
            continue
    except subprocess.TimeoutExpired:
        hangs += 1
        keep = 'tests/fuzz-hang-%d.t' % hangs
        os.rename(path, keep)
        print('HANG saved', keep)
        continue
    os.unlink(path)
print('%d rounds: %d crashes, %d hangs' % (rounds, found, hangs))
sys.exit(1 if found or hangs else 0)
