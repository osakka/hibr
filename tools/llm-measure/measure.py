#!/usr/bin/env python3
"""Measure what docs/llm.md is worth to a model writing hibr.

    measure.py prompt [--page]        print the prompt, with or without the page
    measure.py score RESPONSE...      run each response's scripts and tally

A model is given the tasks below and writes one script per task, in one
response, with no chance to run anything. Each script is run under hibr
with fixed arguments and input, and its standard output compared with what
the task asked for. The tasks come in three groups, reported apart:

  plain    ordinary shell work: does hibr break what a bash writer writes?
  trap     a bash idiom that means something else in hibr
  feature  something only hibr has, asked for by what it does

A dozen tasks on a couple of models is a signal about the page, not a
benchmark. Scripts that could touch the machine are refused, not run.
"""
import os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HIBR = os.environ.get("HIBR", os.path.join(ROOT, "build", "hibr"))

TASKS = [
    ("P1", "plain",
     "Read integers from standard input, one per line, and print their sum.",
     [], "3\n4\n5\n", "12", []),
    ("P2", "plain",
     "Print each command-line argument on its own line, prefixed by its "
     "position and a colon and a space: arguments `\"a b\" c` print `1: a b` "
     "then `2: c`.",
     ["a b", "c"], "", "1: a b\n2: c", []),
    ("P3", "plain",
     "Standard input has lines of the form `name:score`. Print, one per "
     "line and in input order, the names whose score is 50 or more.",
     [], "ana:70\nbob:40\ncy:50\n", "ana\ncy", []),
    ("T1", "trap",
     "Set `line=\"2026-09-30 ERROR disk full\"`. With a regular expression "
     "and its capture groups, and no external command, print the level and "
     "the message joined by a bar: `ERROR|disk full`.",
     [], "", "ERROR|disk full", []),
    ("T2", "trap",
     "Make an associative array `m` holding a=1, b=2 and c=3, and set "
     "`k=b`. Remove the element whose key is held in `k`, then print the "
     "remaining keys in alphabetical order, separated by spaces.",
     [], "", "a c", []),
    ("T3", "trap",
     "Standard input has one word per line; words may contain dashes and "
     "plus signs. Print each distinct word and how many times it occurred, "
     "as `word count`, in the order each word first appeared.",
     [], "a-1\nb+2\na-1\nc\n", "a-1 2\nb+2 1\nc 1", []),
    ("T4", "trap",
     "Make an associative array whose key `content-type` holds "
     "`text/plain` and whose key `x-id` holds `7`, then print both values "
     "separated by a space.",
     [], "", "text/plain 7", []),
    ("F1", "feature",
     "Standard input is the JSON document "
     "`{\"user\":{\"name\":\"ana\",\"roles\":[\"admin\",\"dev\"]}}`. With no "
     "external command (no jq, python or similar), print the user's name, "
     "a space, and the roles joined by commas: `ana admin,dev`.",
     [], '{"user":{"name":"ana","roles":["admin","dev"]}}\n', "ana admin,dev",
     [r"\bjq\b", r"\bpython", r"\bnode\b", r"\bperl\b"]),
    ("F2", "feature",
     "Write a function `add` that hands back the sum of two integers "
     "through hibr's result slot -- no command substitution, no subshell, "
     "no printing inside the function. Call it with 2 and 40, keep the "
     "result in `s`, and print `s`.",
     [], "", "42", [r"\$\((?!\()", r"`"]),
    ("F3", "feature",
     "Using hibr's declared command-line arguments -- not getopts and not "
     "a loop of your own over the arguments -- accept `-v`/`--verbose` (a "
     "flag), `-n`/`--count` (an integer, default 1) and any further "
     "arguments, then print `verbose=V count=N rest=R`, where V is 1 or 0 "
     "and R is the further arguments separated by spaces. Run as "
     "`script -v --count 3 f1 f2` it prints `verbose=1 count=3 rest=f1 f2`.",
     ["-v", "--count", "3", "f1", "f2"], "", "verbose=1 count=3 rest=f1 f2",
     [r"\bgetopts?\b", r"\bshift\b"]),
    ("F4", "feature",
     "Write a function `fetch` that fails with the message `no network`, "
     "using hibr's own way of failing with a message. Call it so that the "
     "failure is caught rather than ending the script, and print `caught: ` "
     "followed by the message.",
     [], "", "caught: no network", []),
    ("F5", "feature",
     "In one nested map, record that user omar has role admin and user ana "
     "has role dev. Print the user names in the order they were added, "
     "separated by a space, then on the next line ana's role.",
     [], "", "omar ana\ndev", []),
    ("F6", "feature",
     "Sort the command-line arguments numerically, in the shell's own "
     "process -- no sort command, no pipeline -- and print them separated "
     "by spaces.",
     ["10", "9", "100", "1"], "", "1 9 10 100",
     [r"(^|[|;&]\s*|\$\(\s*)sort\b", r"(?<!\|)\|(?!\|)"]),
]

HEAD = """You are writing scripts for hibr, a shell. %s

Write one complete script for each task below. Write from this text alone:
do not run, read, search or look anything up. Reply with the scripts and
nothing else, each introduced by a line `=== ID` (for example `=== P1`), and
end with a line `=== END`. No code fences, no commentary. After `=== END`,
add one line saying whether you ran or looked up anything.

"""
ONE = "hibr runs bash scripts and adds features of its own."

DANGER = re.compile(r"\bsudo\b|\brm\b|\bmv\b|\bchmod\b|\bchown\b|\bcurl\b|"
                    r"\bwget\b|\bdd\b|\bmkfs|\bkill|\breboot\b|\bshutdown\b|"
                    r">\s*/(?!dev/null)|/dev/(tcp|udp|tls)|\blisten\b|\bexec\b")


def prompt(page):
    if page:
        with open(os.path.join(ROOT, "docs", "llm.md")) as f:
            intro = ONE + " Its reference follows.\n\n" + f.read()
    else:
        intro = ONE
    out = HEAD % intro
    for tid, _, text, *_ in TASKS:
        out += "%s. %s\n\n" % (tid, text)
    return out


def split(text):
    got, cur, buf = {}, None, []
    for line in text.splitlines():
        m = re.match(r"^===\s*([A-Z][0-9]+|END)\s*$", line.strip())
        if m:
            if cur:
                got[cur] = "\n".join(buf).strip("\n") + "\n"
            cur = None if m.group(1) == "END" else m.group(1)
            buf = []
            if m.group(1) == "END":
                break
        elif cur:
            buf.append(line)
    return got


def run(tid, script, args, stdin):
    d = tempfile.mkdtemp(prefix="hibr-measure-")
    try:
        p = os.path.join(d, "script")
        with open(p, "w") as f:
            f.write(script)
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d,
               "XDG_CONFIG_HOME": d, "XDG_STATE_HOME": d, "XDG_DATA_HOME": d,
               "LANG": os.environ.get("LANG", "C.UTF-8")}
        r = subprocess.run(["timeout", "5", HIBR, p] + args, cwd=d, env=env,
                           input=stdin, capture_output=True, text=True)
        return r.stdout, r.stderr
    finally:
        shutil.rmtree(d, True)


def norm(s):
    return "\n".join(l.rstrip() for l in s.strip("\n").splitlines())


def score(path, verbose):
    with open(path) as f:
        got = split(f.read())
    tally = {"plain": [0, 0], "trap": [0, 0], "feature": [0, 0]}
    lines = []
    for tid, grp, _, args, stdin, want, forbid in TASKS:
        tally[grp][1] += 1
        s = got.get(tid)
        if s is None:
            lines.append("%s  missing" % tid)
            continue
        if DANGER.search(s):
            lines.append("%s  refused: could touch the machine" % tid)
            continue
        bad = [f for f in forbid if re.search(f, s, re.M)]
        out, err = run(tid, s, args, stdin)
        ok = norm(out) == norm(want) and not bad
        if ok:
            tally[grp][0] += 1
        why = "ok" if ok else ("used what the task ruled out" if bad and
                               norm(out) == norm(want) else "wrong output")
        lines.append("%s  %s" % (tid, why))
        if verbose and not ok:
            lines.append("    want: %r\n    got:  %r\n    err:  %r" %
                         (want, out, err.strip()[:300]))
    return tally, lines


def main():
    a = sys.argv[1:]
    if a[:1] == ["prompt"]:
        sys.stdout.write(prompt("--page" in a))
        return
    if a[:1] == ["score"]:
        verbose = "-v" in a
        for path in [x for x in a[1:] if x != "-v"]:
            tally, lines = score(path, verbose)
            print("==", os.path.basename(path))
            for l in lines:
                print("  " + l)
            print("  " + "  ".join("%s %d/%d" % (g, t[0], t[1])
                                   for g, t in tally.items()))
        return
    print(__doc__.strip())
    sys.exit(2)


if __name__ == "__main__":
    main()
