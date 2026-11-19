#!/usr/bin/env python3
"""Every call the desktop makes into an installable's code through the seam
(dt_appfn and the four that go through it, wm/seam.hibr), with what the
caller does with the answer -- which is what decides, once apps run in
processes of their own (ADR 0050), whether the call is a round trip the
desktop waits for or a message it sends and goes on. Read by the inspect
module and the source, never run.

    python3 tools/seamcalls.py [--write|--check]

  use   value    bound with `:=`: the caller reads what the callback ret
        status   its status decides something: tested (if, while, !, && or
                 || after it, a `then`), the function's tail (so its status
                 is the caller's caller's), or `$?` read on the next line
        none     nothing reads the answer
  how   the evidence: tested, tail, return (a bare return after it), $?, :=,
        RET (read in the lines after it); tail-unread and return-unread when
        it is its function's answer and none of that function's callers
        reads it
  met   ask, push or tell, the kind's (KINDS below); shared code is no
        exception, since the desktop runs it too -- a text field typing
        into its window, a scrollbar dragged

Each kind is decided once, in KINDS: how it is met and what happens when
no answer comes. A kind no entry decides, and a told kind whose answer some
caller reads, both fail the check -- so a new one is a decision somebody
makes rather than a default.

--write puts programs/desktop/seamcalls.tsv; --check fails when the table
differs from the source, or a site has no decision.
"""
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DESK = os.path.join(ROOT, "programs", "desktop")
sys.path.insert(0, os.path.join(ROOT, "tests"))
from inspectlib import records

OUT = os.path.join(DESK, "seamcalls.tsv")
SEAMS = ("dt_appfn", "dt_wincall", "dt_winget", "dt_hookfn", "dt_prefixfn", "sv_call")

# How each kind of call is met once its code runs in another process, and
# what the desktop does when no answer comes: the decision is per kind,
# because the wire is per kind, and every kind a site resolves to must be
# here. met is one of
#   ask   a round trip: the desktop waits, at most DT_HOLDMS, for the answer
#   push  the app sends it when it changes, and the desktop uses the last it
#         sent -- a call that would be every frame (a draw, a dirty check,
#         a menu table) never waits on a process
#   tell  sent, and the desktop goes on; the requests it makes arrive in
#         order and the next frame shows them. A tell kind whose answer any
#         site uses is refused here
# and noanswer is what happens when an ask is not answered in time, or a
# push was never made.
NOWIN = "the window is marked not responding"
KINDS = {
    # A window's own callbacks.
    "_draw": ("push", "its last cells stay on screen, and " + NOWIN),
    "_dirty": ("push", "clean: nothing new was pushed"),
    "_paintsall": ("push", "the window is taken not to paint every cell"),
    "_menus": ("push", "the last menus it declared"),
    "_context": ("push", "the last context menu it declared"),
    "_titlemenu": ("push", "the last title menu it declared"),
    "_appname": ("push", "its registered name"),
    "_size": ("push", "its registered size"),
    "_awake": ("push", "asleep: the saver may start"),
    "_key": ("ask", "declined after DT_HOLDMS for a key the desktop also acts on, the desktop's action runs and the key is revoked; any other key is told"),
    "_edit": ("ask", "nothing copied, cut or undone, and " + NOWIN + "; a paste is told"),
    "_signin": ("ask", "nothing filled, and " + NOWIN),
    "_canclose": ("ask", NOWIN + ", and Close asks again with Close Anyway"),
    "_close": ("ask", "after DT_HOLDMS the window goes; its process finishes its own close and is ended only if it has not gone a few seconds later"),
    "_stash": ("ask", "after DT_HOLDMS the restart goes on and the window reopens fresh"),
    "_about": ("tell", "-"), "_click": ("tell", "-"), "_mouse": ("tell", "-"), "_wheel": ("tell", "-"),
    "_hover": ("tell", "-"), "_focus": ("tell", "-"), "_drop": ("tell", "-"), "_fullscreen": ("tell", "-"),
    "_idle": ("tell", "-"), "_launch": ("tell", "-"), "_open": ("tell", "-"), "_refresh": ("tell", "-"),
    "_resume": ("tell", "-"), "_savefile": ("tell", "-"), "_signfill": ("tell", "-"),
    # A bar item's.
    "bar:draw": ("push", "its last cells"),
    "bar:popdraw": ("push", "its last cells"),
    "bar:press": ("ask", "declined: the item's own command runs"),
    "bar:popkey": ("ask", "declined: the key closes the pop-up and goes where it would have"),
    "bar:do": ("tell", "-"), "bar:popmouse": ("tell", "-"), "bar:sfn": ("tell", "-"),
    "bar:stop": ("tell", "-"), "bar:wheel": ("tell", "-"),
    # A Control Panel pane's, and a row's setter and namer.
    "pane_rows": ("push", "the last rows it declared"),
    "pane_draw": ("push", "its last cells"),
    "row:vname": ("push", "the value as stored, unnamed"),
    "pane_key": ("ask", "declined: the panel's own keys"),
    "pane_do": ("ask", "declined"),
    "pane_mouse": ("ask", "declined: the panel's own click"),
    "row:set": ("ask", "the setting unchanged, and the pane marked not responding"),
    "pane_click": ("tell", "-"), "pane_wheel": ("tell", "-"), "pane_drop": ("tell", "-"),
    "pane_order": ("tell", "-"), "pane_slide": ("tell", "-"),
    "_scroll": ("tell", "-"),
    # The words a list of choices shows for each value, named by the code
    # that opened it: sent with the list when it opens.
    "list:namer": ("push", "the value itself"),
    # A Control Strip module's, a location's, a saver's.
    "strip_draw": ("push", "its last cells"),
    "strip_shown": ("push", "shown"),
    "strip_click": ("tell", "-"),
    "location_can": ("ask", "refused: the location did not answer"),
    "location_ok": ("ask", "refused, with the reason the location gave (its DT_WHY comes back with the answer)"),
    "saver_start": ("push", "its last cells"),
    "saver_frame": ("push", "its last cells"),
    # A callback an app handed the desktop: a dialog's answer, a pick, a
    # job's end, a note's action.
    "handed": ("ask", "the dialog stays, and " + NOWIN),
    # Registered hooks.
    "hook:DT_OPENVERBS": ("ask", "the verb is not offered"),
    "hook:DT_BARHOOKS": ("push", "what it last set"),
    "hook:DT_UNLOADFN": ("ask", "after DT_HOLDMS the code goes anyway"),
    "hook:DT_RELOADFN": ("tell", "-"), "hook:DT_ATSTART": ("tell", "-"), "hook:DT_BUS": ("tell", "-"),
    "hook:DT_MODEHOOKS": ("tell", "-"), "hook:DT_TICKERS": ("tell", "-"),
}


def files():
    out = glob.glob(os.path.join(DESK, "**", "*.hibr"), recursive=True)
    return sorted(f for f in out if "/tests/" not in f)


def logical(lines, i):
    """The call's logical line, continuations joined, and its last index."""
    j, text = i, lines[i]
    while re.search(r"(\\|&&|\|\||\|)\s*$", lines[j]) and j + 1 < len(lines):
        j += 1
        text = text.rstrip().rstrip("\\") + " " + lines[j].strip()
    return text, j


def words(s):
    """Shell words of s, quotes and $(...) / ${...} kept whole; ; & | split."""
    out, cur, q, i = [], "", None, 0
    while i < len(s):
        c = s[i]
        if q:
            cur += c
            if c == q:
                q = None
        elif c in "\"'":
            q = c
            cur += c
        elif c == "$" and s[i + 1:i + 2] in "({":
            o = s[i + 1]
            cl = ")" if o == "(" else "}"
            d, j = 0, i + 1
            while j < len(s):
                if s[j] == o:
                    d += 1
                elif s[j] == cl:
                    d -= 1
                    if d == 0:
                        break
                j += 1
            cur += s[i:j + 1]
            i = j
        elif c in " \t;&|":
            if cur:
                out.append(cur)
                cur = ""
            if c in ";&|":
                out.append(c)
        else:
            cur += c
        i += 1
    if cur:
        out.append(cur)
    return out


def callback(seam, args):
    if seam in ("dt_prefixfn", "sv_call"):
        return "_" + args[1] if len(args) > 1 else "?"
    if seam == "dt_hookfn":
        return args[0] if args else "?"
    return args[1] if len(args) > 1 else "?"


def skip_to(lines, i, closer):
    depth = 0
    while i < len(lines):
        t = re.sub(r"#.*", "", lines[i])
        for w in re.findall(r"(?:^|[\s;])(if|case|fi|esac)(?=$|[\s;])", t):
            if w in ("if", "case"):
                depth += 1
            elif depth == 0 and w == closer:
                return i
            else:
                depth -= 1
        i += 1
    return i


def tail(lines, j):
    """Whether nothing but closers follow line j up to the function's }."""
    k = j + 1
    if re.search(r";;\s*$", re.sub(r"#.*", "", lines[j]).strip()):
        k = skip_to(lines, k, "esac") + 1
    while k < len(lines):
        t = re.sub(r"#.*", "", lines[k]).strip()
        if not t:
            k += 1
        elif re.fullmatch(r"(fi|esac|done)(\s*;;)?", t):
            k = skip_to(lines, k + 1, "esac") + 1 if t.endswith(";;") else k + 1
        elif re.match(r"(else|elif)\b", t):
            k = skip_to(lines, k + 1, "fi") + 1
        elif re.match(r"[^)(]*\)\s", t):
            k = skip_to(lines, k, "esac") + 1
        else:
            return t == "}"
    return False


CALLS = {}


def calls(name):
    """Every call of name in the platform's files, by name: (lines, index,
    bound, enclosing function)."""
    if not CALLS:
        for f in files():
            lines = open(f, errors="replace").read().split("\n")
            for c in records(f, "calls").get("calls", []):
                if c.get("name"):
                    CALLS.setdefault(c["name"], []).append(
                        (lines, c["span"]["line"] - 1, c.get("bound"), c.get("owner", "")))
    return CALLS.get(name, [])


def statusread(fname, depth):
    """Whether anything reads the status of a function whose tail is a call:
    any of its callers testing it, or passing it on in a tail of its own. A
    function nothing calls by name -- a callback, run from a stored name --
    or a chain past four deep counts as read: when this cannot tell, it
    says yes, which only ever makes a call wait that need not."""
    sites = calls(fname)
    if not fname or not sites or depth > 4:
        return True
    return any(use(lines, i, fname, bound, owner, depth + 1)[0] != "none"
               for lines, i, bound, owner in sites)


def use(lines, i, seam, bound, owner="", depth=0):
    """(use, how) for the call on line i."""
    if bound:
        return "value", ":="
    text, j = logical(lines, i)
    m = re.search(r"(?<![\w$])" + seam + r"\b", text)
    before, after = text[:m.start()], text[m.end():]
    w = words(after)
    rest = " ".join(w[next((k for k, x in enumerate(w) if x in (";", "&", "|", "then")), len(w)):])
    if re.match(r"\| \| (true|:)\b", rest):
        return "none", "|| true"
    if (re.match(r"(& &|\| \||then\b|; then\b|; do\b)", rest) or
            re.search(r"(^|[\s;(])(if|elif|while|until|!)\s*$", before)):
        return "status", "tested"
    if re.search(r"(^|[\s;(])(if|elif|while|until)\s", before) and re.match(r"(; )?then\b|; do\b", rest):
        return "status", "tested"
    if tail(lines, j):
        return ("status", "tail") if statusread(owner, depth) else ("none", "tail-unread")
    k = j + 1
    while k < len(lines) and (not lines[k].strip() or lines[k].strip().startswith("#")):
        k += 1
    nxt = lines[k].strip() if k < len(lines) else ""
    if "$?" in nxt:
        return "status", "$?"
    if nxt == "return":
        return ("status", "return") if statusread(owner, depth) else ("none", "return-unread")
    if re.search(r"\$RET\b|\$\{RET\b|\bret -k\b", " ".join(lines[j + 1:j + 5])):
        return "value", "RET"
    return "none", "-"


def assigned(lines, i, v):
    """What variable v holds at line i: its last assignment in the enclosing
    function, the list a `for` takes it from, or the parameter it is."""
    for k in range(i, -1, -1):
        l = lines[k] if k < i else lines[k].split(v)[0] + v if re.search(r"for %s in" % v, lines[k]) else ""
        if k == i and not l:
            continue
        if k == i:
            l = lines[k]
        if re.match(r"\s*(fn\s+\w+\(|\w+\(\)\s*\{)", l):
            return "param" if re.search(r"\b%s\b" % v, l) else None
        m = re.search(r"(?:^|[\s;])(?:local\s+)?%s=(\S+)" % v, l)
        if m:
            return m.group(1)
        m = re.search(r"for %s in (.*); do" % v, l)
        if m:
            return "for " + m.group(1)
    return None


# What a site calls, as a kind: a window's own callback (_draw, _key...), a
# bar item's (bar:press...), a pane row's (row:set, row:vname), a callback
# an app handed the desktop (handed), or a registered hook (hook:LIST).
HANDED = ("FP[", "DTA[", "DTJ[", "VWP[", "DT_CONFIRM_CMD", "param", "MI[", "DT_NOTEHIST[")
# A callback handed in as a parameter, by the function it is handed to: the
# input method types into the window's own _key, which its callers read
# from DT[$id]["key"].
OWNERKIND = {"dt_imetype": "_key"}
# A global holding a callback the code that set it owns, by name.
GLOBALKIND = {"DT_POPNAME": "list:namer"}


PREFIXKIND = {"system/panel.hibr": "pane", "wm/strip.hibr": "strip", "wm/locations.hibr": "location",
              "wm/copying.hibr": "location",
              "savers/saver.hibr": "saver"}
# Hooks whose list is not named at the call: by the function that calls them.
HOOKOWNER = {"dt_modehook": "DT_MODEHOOKS", "dt_busgive": "DT_BUS", "dt_bar": "DT_BARHOOKS",
             "dt_unload": "DT_UNLOADFN", "dt_reinstate": "DT_RELOADFN"}


def kind(rel, owner, seam, cb, lines, i):
    if seam in ("dt_prefixfn", "sv_call"):
        return PREFIXKIND.get(rel, "?prefix") + cb
    if owner == "dt_barido":
        return "bar:do"
    if owner in OWNERKIND:
        return OWNERKIND[owner]
    if seam == "dt_hookfn" and owner in HOOKOWNER:
        return "hook:" + HOOKOWNER[owner]
    if seam == "dt_hookfn":
        v = re.fullmatch(r'"?\$\{?(\w+)\}?"?', cb)
        src = assigned(lines, i, v.group(1)) if v else None
        m = re.search(r"\$\{(\w+)\[", src or "")
        return "hook:" + (m.group(1) if m else owner)
    v = re.fullmatch(r'"?\$\{?(\w+)\}?"?', cb)
    if v and v.group(1) in GLOBALKIND:
        return GLOBALKIND[v.group(1)]
    src = assigned(lines, i, v.group(1)) if v else cb
    src = src or cb
    if any(h in src for h in HANDED) or "argv" in src or "args" in src:
        return "handed"
    m = re.search(r'DT_BARI\[[^]]*\]\["(\w+)"\]', src)
    if m:
        return "bar:" + m.group(1)
    if "DT_POP" in src:
        return "bar:" + src.split("_")[-1].strip('"')
    m = re.search(r'CP\[[^]]*\]\[[^]]*\]\["(\w+)"\]', src)
    if m:
        return "row:" + m.group(1)
    m = re.match(r"for \"\$\{(\w+)\[@\]\}\"", src)
    if m:
        return "hook:" + m.group(1)
    m = re.search(r'DT\[[^]]*\]\["\$?(\w+)"\]\}?(_\w+)?', src)
    if m:
        return m.group(2) or ("_edit" if m.group(1) == "op" else "_" + m.group(1))
    m = re.search(r"\}?(_\w+)\"?$", src)
    if m:
        return m.group(1)
    if "DT_TICKOWN" in src or owner == "dt_tickers":
        return "hook:DT_TICKERS"
    if "DT_UNLOADFN" in src or "DT_RELOADFN" in src:
        return "hook:" + re.search(r"(DT_\w+FN)", src).group(1)
    if "DT_OPENVERBS" in src:
        return "hook:DT_OPENVERBS"
    if owner == "dt_appfn":
        return "-"
    return "?" + src


def sites():
    out = []
    for f in files():
        lines = open(f, errors="replace").read().split("\n")
        rel = os.path.relpath(f, DESK)
        for c in records(f, "calls").get("calls", []):
            seam, owner = c.get("name"), c.get("owner", "")
            if seam not in SEAMS or owner in SEAMS:
                continue
            i = c["span"]["line"] - 1
            text, _ = logical(lines, i)
            w = words(text[re.search(r"(?<![\w$])" + seam + r"\b", text).end():])
            args = [x for x in w[:4] if x not in (";", "&", "|")]
            cb = callback(seam, args)
            u, how = use(lines, i, seam, c.get("bound"), owner)
            k = kind(rel, owner, seam, cb, lines, i)
            cross = KINDS.get(k, ("?", ""))[0]
            out.append((rel, c["span"]["line"], owner, seam, cb, k, u, how, cross))
    return out


def table(rows):
    head = "# file\tline\tfunction\tseam\tcallback\tkind\tuse\thow\tmet -- tools/seamcalls.py --write (ADR 0050)\n"
    body = "".join("\t".join(map(str, r)) + "\n" for r in rows)
    kinds = "".join("#kind\t%s\t%s\t%s\n" % (k, KINDS[k][0], KINDS[k][1]) for k in sorted(KINDS))
    return head + body + kinds


def main():
    rows = sites()
    bad = 0
    for r in rows:
        if r[8] == "?":
            print("seamcalls: %s:%d %s calls %s, of kind %s, which KINDS does not decide"
                  % (r[0], r[1], r[2], r[4], r[5]), file=sys.stderr)
            bad += 1
        elif r[8] == "tell" and r[6] != "none":
            print("seamcalls: %s:%d %s reads the %s of %s, a kind that is told: make it ask or push"
                  % (r[0], r[1], r[2], r[6], r[5]), file=sys.stderr)
            bad += 1
    stale = [k for k in KINDS if not any(r[5] == k for r in rows)]
    for k in stale:
        print("seamcalls: %s is decided here and nothing calls it" % k, file=sys.stderr)
    t = table(rows)
    if "--write" in sys.argv:
        open(OUT, "w").write(t)
    elif "--check" in sys.argv:
        old = open(OUT).read() if os.path.exists(OUT) else ""
        strip = lambda s: sorted(re.sub(r"^([^\t]*)\t\d+\t", r"\1\t", l) for l in s.splitlines()
                                 if not l.startswith("# "))
        if strip(old) != strip(t):
            a, b = set(strip(old)), set(strip(t))
            for l in sorted(a - b):
                print("seamcalls: gone or changed: " + l, file=sys.stderr)
            for l in sorted(b - a):
                print("seamcalls: new or changed: " + l, file=sys.stderr)
            print("seamcalls: run tools/seamcalls.py --write and read the diff", file=sys.stderr)
            return 1
    else:
        sys.stdout.write(t)
    n = {}
    for r in rows:
        n[r[8]] = n.get(r[8], 0) + 1
    print("seamcalls: %d calls of %d kinds: %s" % (len(rows), len({r[5] for r in rows}),
          ", ".join("%s %d" % kv for kv in sorted(n.items()))), file=sys.stderr)
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())
