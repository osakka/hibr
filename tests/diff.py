#!/usr/bin/env python3
"""Generate shell snippets, run them under hibr and bash, and diff the results.

tests/fuzz.py asks whether the parser survives. This asks the harder question:
whether the answer is the same one bash gives.  Everything generated is
deterministic and touches nothing — no $RANDOM, no $$, no clock, no file
system, no process but the shell itself.

    python3 tests/diff.py [rounds] [--seed N] [--shell path] [--ref path]

A difference is printed with the snippet that caused it and saved under
tests/diff-NNN.sh for triage.  Divergences hibr makes on purpose are listed in
DELIBERATE and never reported; each names the decision record that argues for
it.
"""
import os, random, re, subprocess, sys

SHELL = "./build/hibr"
REF = "bash"

# Deliberate: a snippet is skipped when one of these can fire in it.  Each is
# a decision record, not a bug, and each pattern is written to match only the
# construct it names -- a filter that also catches ${x} or a function body
# would hide real differences behind a true statement.  See docs/adr/.
DELIBERATE = [
    (re.compile(r"\{[^}]*,[^}]*\}|\{[0-9a-z]+\.\.[0-9a-z]+"),
     "brace expansion is literal-only -- 0007"),
    (re.compile(r"\bpipefail\b"), "no pipefail -- 0002"),
    (re.compile(r"\*\*"), "globstar is always on -- 0008"),
    (re.compile(r"\$\{[^}]*:\?"), "our fatal-expansion status is 1, bash's 127"),
    (re.compile(r"\bset -u\b"), "same"),
    (re.compile(r"BASH_REMATCH"), "captures land in M -- 0004"),
    (re.compile(r"\$\{!?e\[[*@]\]\}"),
     "our maps keep insertion order, bash's hash order -- 0006"),
    # An arithmetic expansion that can fail -- division, or an operand that
    # may be empty.  bash then drops the rest of the and-or list; we drop the
    # command and carry on, which is the same across lines and differs within
    # one.
    (re.compile(r"\$\(\([^)]*[/%]"), "arithmetic that may divide by zero"),
    (re.compile(r"\$\(\([^)]*\$c\b"), "arithmetic on a possibly empty operand"),
    (re.compile(r"\be\[-"),
     "one container, so a negative subscript counts back from the highest "
     "key even on a map; bash has no order there -- 0006"),
]

V = ["a", "b", "c"]
NUM = ["0", "1", "2", "7", "-1", "10"]
TXT = ["x", "ab", "a b", "", "a-b", "A", "abc"]


def lit(r):
    return r.choice(NUM + TXT)


def word(r, d=0):
    """A word: a literal, an expansion of one, or arithmetic."""
    k = r.randint(0, 15 if d < 2 else 3)
    v = r.choice(V)
    if k == 0:
        return '"%s"' % lit(r)
    if k == 1:
        return "'%s'" % lit(r)
    if k == 2:
        return '"$%s"' % v
    if k == 3:
        return "$%s" % v
    if k == 4:
        return '"${%s:-%s}"' % (v, lit(r))
    if k == 5:
        return '"${#%s}"' % v
    if k == 6:
        return '"${%s%s%s}"' % (v, r.choice(["#", "##", "%", "%%"]), r.choice("ab*"))
    if k == 7:
        return '"${%s/%s/%s}"' % (v, r.choice("ab"), r.choice("XY"))
    if k == 8:
        return "$((%s %s %s))" % (
            r.choice(NUM + ["$" + v]), r.choice("+-*/%"), r.choice(["1", "2", "3"]))
    if k == 9:
        return '"${%s:%s:%s}"' % (v, r.randint(0, 2), r.randint(1, 3))
    if k == 10:
        return '"${%s^^}"' % v if r.random() < 0.5 else '"${%s,,}"' % v
    if k == 11:
        return '"${%s/%s%s/%s}"' % (v, r.choice("#%"), r.choice("ab"),
                                    r.choice("XY"))
    if k == 12:
        return '"${e[%s]}"' % r.choice(['0', '1', '"k"', 'i', '-1', 'k'])
    if k == 13:
        return r.choice(['"${e[*]}"', '"${!e[*]}"', '"${#e[@]}"', '${e[@]}'])
    if k == 14:
        return '"${!%s}"' % r.choice(["ref", "v"])
    return '"$(echo %s)"' % r.choice(NUM + ["x", "a b"])


def cond(r):
    v = r.choice(V)
    k = r.randint(0, 4)
    if k == 0:
        return '[ "$%s" = %s ]' % (v, word(r, 2))
    if k == 1:
        return '[ -n "$%s" ]' % v
    if k == 2:
        return '[ -z "$%s" ]' % v
    if k == 3:
        return '[[ "$%s" == %s ]]' % (v, r.choice(['"a*"', '"*b"', '"?"', '"ab"']))
    return "[ %s -%s %s ]" % (r.choice(NUM), r.choice(["eq", "ne", "lt", "gt"]),
                              r.choice(NUM))


def stmt(r, d=0):
    k = r.randint(0, 15 if d < 2 else 4)
    v = r.choice(V)
    if k == 0:
        return "%s=%s" % (v, word(r, d))
    if k == 1:
        return "echo %s" % word(r, d)
    if k == 2:
        return "printf '[%%s]' %s; echo" % word(r, d)
    if k == 3:
        return "%s=(%s %s); echo \"${%s[*]} ${#%s[@]}\"" % (
            v, word(r, 2), word(r, 2), v, v)
    if k == 4:
        return "echo $((%s %s %s))" % (r.choice(NUM), r.choice("+-*"),
                                       r.choice(["1", "2", "4"]))
    if k == 5:
        return "if %s; then echo yes; else echo no; fi" % cond(r)
    if k == 6:
        return "for i in %s %s; do echo \"$i\"; done" % (word(r, 2), word(r, 2))
    if k == 7:
        return "case %s in a*) echo A ;; %s) echo B ;; *) echo C ;; esac" % (
            word(r, 2), r.choice(['"ab"', "b*", "?"]))
    if k == 8:
        return "f() { echo \"in:$1\"; }; f %s" % word(r, 2)
    if k == 9:
        return "%s; %s" % (stmt(r, d + 1), stmt(r, d + 1))
    if k == 10:
        return 'e[%s]=%s; echo "${e[%s]} ${!e[*]}"' % (
            r.choice(['0', '"k"', 'k', 'i']), word(r, 2),
            r.choice(['0', '"k"', 'k', 'i']))
    if k == 11:
        return 'IFS=%s; set -- $%s; echo "$# [$*]"; IFS=" "' % (
            r.choice([":", ",", '" "']), r.choice(V))
    if k == 12:
        return 'n=0; while [ $n -lt %s ]; do n=$((n+1)); done; echo $n' % (
            r.randint(1, 3))
    if k == 13:
        return 'read -r p q <<< %s; echo "[$p][$q]"' % word(r, 2)
    if k == 14:
        return 'unset e[%s]; echo "${!e[*]}"' % r.choice(['0', '"k"', '1'])
    return 'printf "%%s|%%s\\n" %s %s' % (word(r, 2), word(r, 2))


def snippet(r):
    lines = ["a=ab", "b=2", "c=", "unset d", "i=1", "v=a", "ref=a",
             "declare -A e 2>/dev/null || true", 'e[k]=K', "e[0]=Z"]
    for _ in range(r.randint(1, 4)):
        lines.append(stmt(r))
    return "\n".join(lines) + "\n"


def run(sh, src):
    try:
        p = subprocess.run([sh], input=src, capture_output=True, text=True,
                           timeout=5, cwd="/tmp")
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        return "timeout", ""
    except Exception as e:
        return "error", str(e)


def main():
    rounds = 200
    seed = int(os.environ.get("SEED", 1))
    shell, ref = SHELL, REF
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--seed":
            seed = int(args[i + 1]); i += 2
        elif args[i] == "--shell":
            shell = args[i + 1]; i += 2
        elif args[i] == "--ref":
            ref = args[i + 1]; i += 2
        else:
            rounds = int(args[i]); i += 1
    shell = os.path.abspath(shell) if "/" in shell else shell
    ref = os.path.abspath(ref) if "/" in ref else ref
    r = random.Random(seed)
    diffs = skipped = 0
    for n in range(rounds):
        src = snippet(r)
        why = next((w for t, w in DELIBERATE if t.search(src)), None)
        if why:
            skipped += 1
            continue
        a = run(ref, src)
        b = run(shell, src)
        if a == b:
            continue
        diffs += 1
        path = "tests/diff-%03d.sh" % diffs
        open(path, "w").write(src)
        print("--- difference %d, saved as %s" % (diffs, path))
        print(src.rstrip())
        print("    %s: rc=%s out=%r" % (ref, a[0], a[1]))
        print("    %s: rc=%s out=%r" % (shell, b[0], b[1]))
        if diffs >= 10:
            print("\nstopping after 10")
            break
    print("\n%d rounds: %d differences, %d skipped as deliberate"
          % (rounds, diffs, skipped))
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
