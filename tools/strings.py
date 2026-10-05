#!/usr/bin/env python3
"""Find every string the desktop shows a person, for translation (#68).

    python3 tools/strings.py            # a census: counts per kind, per file
    python3 tools/strings.py --list     # every string, file:line kind text
    python3 tools/strings.py --keys     # the sorted catalogue keys

Reads examples/desktop/**/*.hibr as the shell would: quote state is carried
across lines (a quoted string may span several), commands are split at
newlines, ;, &&, ||, | and the braces, and a call to one of the functions
that draw text a person reads (SINKS) has its text arguments taken:

    literal   plain quoted text, translated where it is drawn
    template  text with an expansion in it -- needs a template key
    raw       text drawn straight with `console put`
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The text arguments of each sink: positions (1-based), or "pairs:N" for
# label/tag pairs from N on, or "last".
SINKS = {
    "dt_menu": [1], "dt_item": [1], "dt_sub": [1], "dt_dim": [1],
    "dt_row": [3], "dt_app": [2], "cp_pane": [2], "dt_note": [1],
    "dt_notep": [2], "dt_confirm": [1], "dt_button": [4],
    "dt_buttons": "pairs:6", "dt_dlgbtns": "pairs:6", "dt_check": [5],
    "dt_new": [1], "dt_retitle": [2], "dt_iconadd": [3],
    "console": "put", "dt_tr": [1], "dt_trn": [2, 3], "dt_tally": [3], "dt_tput": [4],
}
SEP = {";", "&&", "||", "|", "{", "}", "(", ")", "then", "do", "else", "elif", "!"}


def words(src):
    """Commands as lists of (text, literal, line): literal when every part
    of the word was quoted or plain text with no expansion."""
    cmds, cur, w, lit, q, i, ln, wl = [], [], None, True, None, 0, 1, 1
    n = len(src)

    def end_word():
        nonlocal w, lit
        if w is not None:
            cur.append((w, lit, wl))
        w, lit = None, True

    def end_cmd():
        nonlocal cur
        end_word()
        if cur:
            cmds.append(cur)
        cur = []

    while i < n:
        c = src[i]
        if c == "\n":
            ln += 1
        if q == "'":
            if c == "'":
                q = None
            else:
                w += c
            i += 1
            continue
        if q == '"':
            if c == '"':
                q = None
            elif c == "\\" and i + 1 < n:
                w += src[i + 1]
                i += 2
                continue
            else:
                if c == "$" or c == "`":
                    lit = False
                w += c
            i += 1
            continue
        if c == "#" and w is None:
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c in " \t":
            end_word()
            i += 1
            continue
        if c == "\n":
            end_cmd()
            i += 1
            continue
        if c == "\\" and i + 1 < n and src[i + 1] == "\n":
            ln += 1
            i += 2
            continue
        if c in ";|&(){}":
            end_word()
            if src[i:i + 2] in ("&&", "||"):
                i += 2
            else:
                i += 1
            end_cmd()
            continue
        if w is None:
            w, wl = "", ln
        if c in "'\"":
            q = c
        elif c == "$" or c == "`":
            lit = False
            w += c
        elif c == "\\" and i + 1 < n:
            w += src[i + 1]
            i += 1
        else:
            w += c
        i += 1
    end_cmd()
    out = []
    for cmd in cmds:
        while cmd and cmd[0][0] in SEP:
            cmd = cmd[1:]
        if cmd:
            out.append(cmd)
    return out


def texts(cmd):
    """The text arguments of a sink call: (word, literal, line)."""
    if len(cmd) > 2 and cmd[1][0] == ":=":
        cmd = cmd[2:]
    name = cmd[0][0]
    spec = SINKS.get(name)
    args = cmd[1:]
    if spec is None:
        return []
    if spec == "put":
        if not args or args[0][0] != "put":
            return []
        a = args[1:]
        while a and a[0][0] in ("-r", "-p"):
            a = a[2:] if a[0][0] == "-p" else a[1:]
        return [a[-1] + ("raw",)] if len(a) >= 3 else []
    if isinstance(spec, str) and spec.startswith("pairs:"):
        k = int(spec.split(":")[1])
        return [args[j] + (None,) for j in range(k - 1, len(args), 2)]
    return [args[p - 1] + (None,) for p in spec if p - 1 < len(args)]


ROWTEXT = re.compile(r'\["text"\]="([^"$`\\]*[A-Za-z][^"$`\\]*)"')


def collect():
    rows = []
    for f in sorted(glob.glob(os.path.join(ROOT, "examples/desktop/**/*.hibr"),
                              recursive=True)):
        rel = os.path.relpath(f, ROOT)
        if "/login/" in rel:
            continue
        src = open(f, encoding="utf-8").read()
        for ln, line in enumerate(src.split("\n"), 1):
            if line.lstrip().startswith("#"):
                continue
            for m in ROWTEXT.finditer(line):
                rows.append((rel, ln, "literal", m.group(1)))
        src = src.replace("${GL[ellipsis]}", "…")
        for cmd in words(src):
            for w, lit, ln, raw in texts(cmd):
                if not re.search(r"[A-Za-z]", w):
                    continue
                kind = raw or ("literal" if lit else "template")
                if not lit and re.fullmatch(r"\$\{?[A-Za-z_][A-Za-z0-9_]*(\[[^]]*\])*\}?", w):
                    continue
                rows.append((rel, ln, kind, w))
    return rows


def keys(rows):
    return sorted({r[3] for r in rows if r[2] == "literal"})


if __name__ == "__main__":
    rows = collect()
    lang = os.path.join(ROOT, "examples/desktop/lang")
    if "--write" in sys.argv:
        import json
        os.makedirs(lang, exist_ok=True)
        open(os.path.join(lang, "strings.txt"), "w").write("".join(k + "\n" for k in keys(rows)))
        xx = {"language": "xx", "name": "Pseudo (xx)", "dir": "ltr", "plural": "en",
              "strings": dict((k, "⟦" + k + "⟧") for k in keys(rows))}
        open(os.path.join(lang, "xx.json"), "w").write(
            json.dumps(xx, ensure_ascii=False, indent=1, sort_keys=True) + "\n")
        sys.exit(0)
    if "--check" in sys.argv:
        have = open(os.path.join(lang, "strings.txt")).read().split("\n")[:-1]
        want = keys(rows)
        new = sorted(set(want) - set(have))
        gone = sorted(set(have) - set(want))
        for k in new:
            print("new string, not in lang/strings.txt: %s" % k)
        for k in gone:
            print("in lang/strings.txt, no longer drawn: %s" % k)
        if new or gone:
            print("tools/strings.py --write brings the catalogue up to date")
            sys.exit(1)
        print("every string drawn is in the catalogue (%d)" % len(want))
        sys.exit(0)
    if "--list" in sys.argv:
        for r in rows:
            print("%s:%d %s %s" % r)
    elif "--keys" in sys.argv:
        for k in keys(rows):
            print(k)
    else:
        kinds = {}
        for r in rows:
            kinds[r[2]] = kinds.get(r[2], 0) + 1
        print("strings drawn: %d (%d distinct literal keys)" % (
            len(rows), len({r[3] for r in rows if r[2] == "literal"})))
        for k in ("literal", "template", "raw"):
            print("  %-8s %d" % (k, kinds.get(k, 0)))
        files = {}
        for r in rows:
            files[r[0]] = files.get(r[0], 0) + 1
        for f, c in sorted(files.items(), key=lambda x: -x[1])[:12]:
            print("  %4d %s" % (c, f))
