#!/usr/bin/env python3
"""Find every string the desktop shows a person, for translation (#68).

    python3 tools/strings.py            # a census: counts per kind, per file
    python3 tools/strings.py --list     # every string, file:line kind text
    python3 tools/strings.py --keys     # the sorted catalogue keys

Reads programs/desktop/**/*.hibr as the shell would: quote state is carried
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
    # dt_mark was missing until 0.99.162, so every checkmark item in every
    # menu stayed English in a translated desktop -- and nothing could
    # notice, because 992-strings.t asks whether every string found here is
    # listed, not whether everything that reaches dt_tr was found
    # (Gitea #137).
    "dt_menu": [1], "dt_item": [1], "dt_sub": [1], "dt_dim": [1],
    "dt_barilabel": [2],
    "dt_mark": [1],
    "dt_row": [3], "dt_app": [2], "cp_pane": [2], "dt_note": [1],
    # dt_notify and dt_notifyp were missing for as long as they have
    # existed, which is the dt_mark omission again (Gitea #137): both
    # translate their text at runtime, so a literal note would have been
    # translatable and was simply never collected to be translated. Every
    # call today passes interpolated text, so they add no key here and
    # fourteen templates to #202's count instead.
    "dt_notify": [1], "dt_notifyp": [2],
    "dt_notep": [2], "dt_confirm": [1], "dt_button": [4],
    "dt_buttons": "pairs:6", "dt_dlgbtns": "pairs:6", "dt_check": [5],
    "dt_new": [1], "dt_retitle": [2], "dt_iconadd": [3],
    # dt_wintitle's first argument is the app's own name, which is a key it
    # already has from its dt_app line; its second is a detail with no words
    # of ours in it (Gitea #202).
    "dt_wintitle": [1],
    "console": "put", "dt_tr": [1], "dt_trn": [2, 3], "dt_tally": [3], "dt_tput": [4],
}
# Templates that carry no words of ours, each with the reason it is one: a
# filename, a letter, a value a dropdown shows, a server's own message, a
# title that was already made. Everything *else* that reaches a sink with an
# expansion in it is a string no catalogue can hold -- the key would carry
# somebody's own account name or file -- so this list is the check rather
# than a count: a new one fails tests/992-strings.t instead of quietly
# reading English for ever (Gitea #202). Add to it only with a reason.
VALUEONLY = {
    '${IV[$id][path]##*/}',  # the picture's own filename
    '${c^}',  # one letter of a note's own text
    '${BW_MARKT[$i]:0:40}',  # the bookmark's own title
    '${v^}',  # a value a dropdown is showing
    '${f^^}',  # a column's own name
    '${d#dav://}: $DT_WHY',  # a server's address and the server's own words
    '${GL[bullet]} ${DT[$id][title]}',  # a glyph and a title already made
    '${DT[$id][title]#${GL[bullet]} }',  # the same title with the glyph taken off
    '${n:0:24}',  # the program's own name, cut to fit
    '${t%% [*}',  # a window title with its bracket part cut
    '$a [$detail]',  # dt_wintitle's own brackets: the app name above is the key
    '$what $last',  # a word already translated, and a filename
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
    for f in sorted(glob.glob(os.path.join(ROOT, "programs/desktop/**/*.hibr"),
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
    lang = os.path.join(ROOT, "programs/desktop/lang")
    if "--write" in sys.argv:
        import json
        os.makedirs(lang, exist_ok=True)
        open(os.path.join(lang, "strings.txt"), "w").write("".join(k + "\n" for k in keys(rows)))
        xx = {"language": "xx", "name": "Pseudo (xx)", "dir": "ltr", "plural": "en",
              "strings": dict((k, "⟦" + k + "⟧") for k in keys(rows))}
        open(os.path.join(lang, "xx.json"), "w").write(
            json.dumps(xx, ensure_ascii=False, indent=1, sort_keys=True) + "\n")
        xy = {"language": "xy", "name": "Pseudo right-to-left (xy)", "dir": "rtl", "plural": "en",
              "strings": dict((k, "\u27ea" + k + "\u27eb") for k in keys(rows))}
        open(os.path.join(lang, "xy.json"), "w").write(
            json.dumps(xy, ensure_ascii=False, indent=1, sort_keys=True) + "\n")
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
        bad = [r for r in rows if r[2] == "template" and r[3] not in VALUEONLY]
        for r in bad:
            print("a string with an expansion cannot be a catalogue key, so it "
                  "will never be translated: %s:%d %s" % (r[0], r[1], r[3]))
        if bad:
            print("make it a format and pass the value: dt_tr \"...%s...\" \"$v\"")
        if new or gone:
            print("tools/strings.py --write brings the catalogue up to date")
        if new or gone or bad:
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
