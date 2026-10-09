#!/usr/bin/env python3
"""mermaid: the properties a drawing must have, whatever it looks like.

Mermaid has no conformance suite to run against, so tests/999-mermaid.t
records what the corpus draws and this asserts the things that must be true
of *any* drawing: no two boxes overlap, every edge ends on a box it names,
every run list covers its own row exactly, and the drawing is no bigger
than it says it is. A recorded test says "this is what it looked like"; a
property says "this is what it may never do", and only the second survives
the drawing being improved.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screen import check, report, scratch
import screen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "build", "mods", "mermaid.so")
CORPUS = os.path.join(ROOT, "tests", "mermaid")


def run(script):
    """A hibr script with the module loaded, its output as lines."""
    p = subprocess.run([screen.HIBR, "-c", "mod load %s > /dev/null\n%s"
                        % (MOD, script)], capture_output=True, text=True,
                       cwd=ROOT, timeout=60)
    return p.stdout.splitlines(), p.stderr.strip(), p.returncode


def parsed(path):
    """The graph of a diagram, as a list of fields per item."""
    out, err, rc = run('mermaid parse "%s"' % path)
    return [l.split("\t") for l in out], err, rc


def drawn(path, extra=""):
    """The rows of a diagram and the run list beside each."""
    out, err, rc = run('d := mermaid render %s "%s"\n'
                       'echo "H ${d["kind"]} ${d["w"]} ${d["h"]}"\n'
                       'i=0\n'
                       'while [ "$i" -lt "${#d["text"][@]}" ]; do\n'
                       '  printf "T %%s\\n" "${d["text"][$i]}"\n'
                       '  printf "R %%s\\n" "${d["runs"][$i]}"\n'
                       '  i=$((i + 1))\n'
                       'done' % (extra, path))
    head = [l for l in out if l.startswith("H ")]
    rows = [l[2:] for l in out if l.startswith("T ")]
    runs = [l[2:] for l in out if l.startswith("R ")]
    return (head[0].split() if head else []), rows, runs, err, rc


FILES = sorted(f for f in os.listdir(CORPUS) if f.endswith(".mmd"))
check("the corpus is there", len(FILES) >= 7, FILES)

def kindof(items):
    """What a parsed diagram says it is."""
    for i in items:
        if i[0] == "kind":
            return i[1]
    return ""


# No two boxes overlap. This is the one thing a layout engine exists to get
# right, and the one thing a recorded drawing cannot tell you it has lost:
# a box drawn over another still renders, it just reads as nonsense.
#
# A pie has no boxes, so the check would be vacuously true of one -- which
# is the shape of a test that passes because it never ran. Each kind is
# asked for what it does have instead.
for f in FILES:
    path = os.path.join(CORPUS, f)
    items, err, rc = parsed(path)
    kind = kindof(items)
    if kind == "pie":
        check("%s: a pie has slices" % f,
              len([i for i in items if i[0] == "slice"]) >= 2, items)
        continue
    boxes = [(i[1], int(i[3]), int(i[4]), int(i[5]), int(i[6]))
             for i in items if i[0] in ("node", "part")]
    bad = []
    for a in range(len(boxes)):
        for b in range(a + 1, len(boxes)):
            ia, xa, ya, wa, ha = boxes[a]
            ib, xb, yb, wb, hb = boxes[b]
            if xa < xb + wb and xb < xa + wa and \
               ya < yb + hb and yb < ya + ha:
                bad.append((ia, ib))
    check("%s: no two boxes overlap" % f, not bad and len(boxes) >= 2,
          (bad, boxes))

# Every edge names two nodes that exist. A chain through bend points must
# come out the other side still joining the two things the text said.
for f in FILES:
    items, err, rc = parsed(os.path.join(CORPUS, f))
    if kindof(items) == "pie":
        continue
    ids = {i[1] for i in items if i[0] in ("node", "part")}
    ends = [(i[1], i[2]) for i in items if i[0] in ("edge", "msg")]
    bad = [e for e in ends if e[0] not in ids or e[1] not in ids]
    check("%s: every edge ends on a box it names" % f, ends and not bad,
          (bad, sorted(ids)))

# Every run list covers its own row exactly, in characters -- the contract
# the desktop draws through. A run list one short leaves a cell unpenned;
# one long draws past the end of the line.
for f in FILES:
    head, rows, runs, err, rc = drawn(os.path.join(CORPUS, f), "-w 44")
    bad = []
    for i, (t, r) in enumerate(zip(rows, runs)):
        tot = sum(int(tok.split(":")[1]) for tok in r.split()) if r else 0
        if tot != len(t):
            bad.append((i, tot, len(t), t, r))
    check("%s: every run list covers its row" % f,
          rows and not bad and len(rows) == len(runs), (bad, len(rows),
                                                        len(runs)))
    check("%s: and it says how big it is" % f,
          len(head) == 4 and int(head[3]) == len(rows) and
          int(head[2]) >= max(len(t) for t in rows), (head, len(rows)))
    check("%s: no shell error" % f, not err, err)

# Only the letters the contract names, or a window has no pen for a cell.
LETTERS = set("bteluk.")
for f in FILES:
    head, rows, runs, err, rc = drawn(os.path.join(CORPUS, f), "-w 44")
    seen = {tok.split(":")[0] for r in runs for tok in r.split()}
    check("%s: every style is one the contract names" % f,
          seen and seen <= LETTERS, sorted(seen))

# The ascii set draws every line, box, arrow and bar out of ascii, which is
# what DT_GLYPHSET=ascii needs and what a terminal that cannot show box
# drawing gets. A *label* is the user's own text and stays as they wrote it
# -- `-a` is about the glyphs this module draws, not about the words in the
# diagram, and a CJK label is still CJK. So the check is for the drawing
# characters and not for any byte above 127, which the first version did
# and which failed on the corpus entry that exists to test wide glyphs.
DRAWN = set("\u2500\u2502\u250c\u2510\u2514\u2518\u2534\u252c\u2524"
            "\u251c\u253c\u25bc\u25b2\u25c0\u25b6\u254c\u254e\u2501"
            "\u2503\u256d\u256e\u2570\u256f\u2588\u25cb\u00d7")
for f in FILES:
    head, rows, runs, err, rc = drawn(os.path.join(CORPUS, f), "-a -w 44")
    hi = [t for t in rows if set(t) & DRAWN]
    check("%s: -a draws its own lines in ascii alone" % f,
          rows and not hi, hi[:3])
    plain, pr, pu, _, _ = drawn(os.path.join(CORPUS, f), "-w 44")
    check("%s: and the unicode set does use them" % f,
          f == "pie.mmd" or any(set(t) & DRAWN for t in pr), pr[:3])

# A diagram this release does not understand says so, and says nothing
# else: a half-drawn diagram is wronger than none, which is the rule the
# parser is built on.
NOT_YET = [
    ("classDiagram\n  class A", "kind of diagram"),
    ("erDiagram\n  A ||--|| B : has", "kind of diagram"),
    ("gantt\n  title x", "kind of diagram"),
    ("stateDiagram-v2\n  [*] --> A", "kind of diagram"),
    ("flowchart TD\n  subgraph s\n  A --> B\n  end", "subgraph"),
    ("flowchart TD\n  A[[sub]] --> B", "node shape"),
    ("flowchart TD\n  A[/slant/] --> B", "node shape"),
    ("sequenceDiagram\n  alt yes\n  A->>B: hi\n  end", "sequence blocks"),
    ("flowchart TD\n  A ~~> B", "not a link"),
    ("", "nothing here"),
]
for src, want in NOT_YET:
    out, err, rc = run('d := mermaid render -t %s\n'
                       'echo "${d["kind"]}|${d["why"]}"'
                       % ("'" + src.replace("'", "") + "'"))
    line = out[0] if out else ""
    kind, _, why = line.partition("|")
    check("not understood yet: %s" % (want),
          kind == "" and want in why, (src.split("\n")[0], line))

# And one that is understood says nothing of the sort.
for f in FILES:
    out, err, rc = run('d := mermaid render "%s"\n'
                       'echo "${d["kind"]}|${d["why"]}"'
                       % os.path.join(CORPUS, f))
    kind, _, why = (out[0] if out else "").partition("|")
    check("%s: understood, with nothing to apologise for" % f,
          kind in ("flowchart", "sequence", "pie") and not why,
          (kind, why))

# A diagram with a cycle in it still draws, with the arrow pointing the way
# the text said: the layout reverses such an edge to rank the graph at all,
# and the drawing has to put the head back where it belongs.
head, rows, runs, err, rc = drawn(os.path.join(CORPUS, "flow.mmd"))
up = [i for i, t in enumerate(rows) if "▲" in t]
down = [i for i, t in enumerate(rows) if "▼" in t]
check("a cycle draws an arrow back the way the text said",
      up and down and min(up) < min(down), (up, down))

# A file that is not there is an error and not a crash.
out, err, rc = run("mermaid render /nonesuch/x.mmd")
check("a missing file is said rather than crashed on",
      rc != 0 and "cannot read" in err, (rc, err))

# Two edges between the same pair are two edges, not one drawn twice.
items, err, rc = parsed(scratch("mm-two.mmd"))
open(scratch("mm-two.mmd"), "w").write("flowchart TD\n A --> B\n A --> B\n")
items, err, rc = parsed(scratch("mm-two.mmd"))
check("two edges between one pair are both kept",
      len([i for i in items if i[0] == "edge"]) == 2, items)
os.unlink(scratch("mm-two.mmd"))

# --- the app and Write, through a pty ----------------------------------
#
# Everything above is the module on its own. These two are the whole point
# of it: a window that draws a diagram, and a document that has one in it.

from screen import Term, load, tree, press
import screen as _sc

WM = os.path.join(ROOT, "examples", "desktop", "desktop.hibr")
APPS = 'DT_APPDIRS+=("%s")\n%s\ndt_apps\n' % (
    tree(os.path.join(ROOT, "examples", "desktop", "apps")),
    'CP_PANEDIRS+=("%s")' % tree(os.path.join(ROOT, "examples", "desktop",
                                              "control-panel")))


def session(body, feed=(), wait=0.8):
    """A desktop with the apps loaded, driven and then read."""
    path = scratch("mermaid-app.hibr")
    open(path, "w").write("%s. %s\n%s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load("console"), WM, APPS, body))
    t = Term(path, env={"DT_TICK": "60"}, rows=26, cols=88, settle=0.6)
    t.collect(wait)
    if feed:
        t.send(b"".join(feed) if isinstance(feed, (list, tuple)) else feed)
        t.collect(0.6)
    sc = t.screen()
    t.quit(b"\x1bw", 1.0)
    os.unlink(path)
    return sc, t


# The editor: the text on one side and the drawing on the other. The window
# opens on a diagram of its own rather than an empty buffer, because the one
# difficulty this app has is that nobody can guess the language.
sc, t = session('dt_launch diagram')
check("the editor opens with Mermaid text on one side",
      sc.find("flowchart TD") is not None, sc)
check("and the diagram drawn on the other",
      sc.find("Write Mermaid here") is not None and
      sc.find("Does it draw?") is not None, sc)
box = sc.find("┌")
check("drawn as boxes, which means the module's own cells reached a pane",
      box is not None, sc)

# The diagram is drawn in the theme's colours, run by run, which is the
# whole reason the module answers with styles at all.
r, c = sc.find("Write Mermaid here")
pens = {sc.style(r, c + i)["fg"] for i in range(6)}
edge = sc.find("▼")
check("a node's label and an edge are drawn in different pens",
      edge is not None and sc.style(edge[0], edge[1])["fg"] not in pens,
      (sorted(pens), edge))

# A diagram it does not understand says why, where the diagram would be,
# rather than leaving the half blank.
sc, t = session('id := dt_launch diagram\n'
                'tb_set "$id" "classDiagram"')
# Matched on a fragment that survives the wrapping: the message is wrapped
# to the half it is shown in, so "not understood yet" is split across two
# rows and never appears whole.
check("text it does not understand says so where the diagram would be",
      sc.find("understood yet") is not None, sc)

# Each kind from the Diagram menu, so the language can be found by trying
# it rather than by reading about it somewhere else.
sc, t = session('id := dt_launch diagram\n'
                'dia_sample "$id" sequence')
check("the Diagram menu's sequence sample draws a sequence",
      sc.find("Alice") is not None and sc.find("Hello Bob") is not None, sc)
sc, t = session('id := dt_launch diagram\n'
                'dia_sample "$id" pie')
check("and its pie sample draws a pie, with its shares",
      sc.find("Meetings") is not None and sc.find("%") is not None, sc)

# Write: a ```mermaid block is the diagram where it sits, and its source
# when the cursor is in it -- there is no editing a diagram you cannot see
# the text of.
doc = scratch("mermaid-doc.md")
open(doc, "w").write("""# How it works

The steps:

```mermaid
flowchart LR
    A[Read] --> B[Draw] --> C[Done]
```

And that is all.
""")
sc, t = session('dt_launch write "%s"' % doc)
check("Write draws a ```mermaid block as the diagram",
      sc.find("┌──────┐") is not None
      and sc.find("Read") is not None and sc.find("flowchart LR") is None,
      sc)
check("and the text around it is still where it was",
      sc.find("How it works") is not None and sc.find("And that is all")
      is not None, sc)
# The block takes the diagram's rows and not its source's, so what follows
# it sits exactly the diagram's height below: a blank, three rows of
# diagram, a blank, and the line itself. The source is four lines, so this
# number is what says the block was drawn rather than shown.
a = sc.find("The steps:")
b = sc.find("And that is all")
check("the block takes the diagram's own height, not its source's",
      a is not None and b is not None and b[0] - a[0] == 6, (a, b))

sc, t = session('dt_launch write "%s"' % doc, feed=[b"\x1b[B"] * 5)
check("the cursor moved into the block shows its source again",
      sc.find("flowchart LR") is not None and
      sc.find("```mermaid") is not None, sc)
# And the cursor is where the keys put it, not where the diagram's rows
# would have left it: every loop that counts rows has to apply the same
# rule, which is what this catches.
cur = [(r, c) for r in range(sc.rows) for c in range(sc.cols)
       if sc.at(r, c) == "f" and sc.style(r, c)["bg"] == "#cbd5e0"]
fl = sc.find("flowchart LR")
check("with the cursor on the line the keys put it on",
      fl is not None and cur and cur[0] == fl, (cur[:2], fl))
os.unlink(doc)

check("no shell error in any session", not _sc.ERRS,
      "\n".join(dict.fromkeys(_sc.ERRS)))

report(None)
