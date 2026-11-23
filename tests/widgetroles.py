#!/usr/bin/env python3
"""The themed controls and the LCD roles, through a pty (Gitea #603, #604).

A slider is drawn as its parts -- its left end, the track filled up to the
knob, the knob, the track ahead, its right end -- each in its own role
(track, trackfill, knob), or the dim colour, the accent and the ink where a
scheme leaves them out; a fader is the same stood on end, min at the
bottom, and dt_fadepos its inverse. In the blit glyph set both are Blit's
own pieces (ADR 0237), read here from the tree's table, since this box's
Blit may be older. An icon button (dt_button given a width) is a key cap in
the button roles: in the blit set Blit's key ends and the bevel on its icon
(SGR 51:6 raised, 51:7 pressed) with the dark edge in SGR 58; elsewhere no
framing at all, and a key that is down takes its dark edge for a face. A lit
key is down and filled with the accent. The LCD roles reach a desktop from
the scheme a settings file names, written before they existed, and applying
a scheme sets every role. Run it directly:
python3 tests/widgetroles.py [path-to-hibr]
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import Term, check, report, load, tree, scratch

if len(sys.argv) > 1:
    screen.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")
WM = tree("programs/desktop/desktop.hibr")
LOOK = tree("programs/desktop/control-panel/appearance.hibr")
ROWS, COLS = 24, 80
TABLE = json.load(open(tree("programs/desktop/wm/blit-glyphs.json")))["glyphs"]


def bg(name):
    return chr(int(TABLE[name], 16))


# One window: a marker, a slider at 50 of 0-100 ten columns wide,
# a fader at 40 six rows high, three icon buttons -- raised, pressed, lit --
# and what dt_fadepos answers for the fader's top row, its bottom row, and a
# quarter of the way down its third.
WR = ('wr_draw() { local a b c\n'
      '  dt_wclear "$1"\n'
      '  console pen "$DT_INK" "$DT_FACE"\n'
      '  console put -p "w$1" 1 1 "@"\n'
      '  dt_slider "$1" 1 2 10 50 0 100 s\n'
      '  dt_fader "$1" 1 14 6 40 0 100 f\n'
      '  dt_button "$1" 3 2 "${GL[play]}" 0 k1 3\n'
      '  dt_button "$1" 3 6 "${GL[stop]}" 0 k2 3 pressed\n'
      '  dt_button "$1" 3 10 "${GL[next]}" 0 k3 3 lit\n'
      '  a := dt_fadepos 1 1 6 0 100; b := dt_fadepos 6 1 6 0 100\n'
      '  c := dt_fadepos 3 1 6 0 100 250\n'
      '  console pen "$DT_INK" "$DT_FACE"\n'
      '  console put -p "w$1" 8 2 "fp $a $b $c lcd $DT_LCD $DT_LCDINK track [$DT_TRACK]"; }\n'
      'dt_app wr "Roles" 11 50\n'
      'dt_new "Roles" 11 50 2 2 wr\n')


def run(env=None, pre="", look=False):
    """The desktop with that window; its last screen."""
    path = scratch("widgetroles.hibr")
    open(path, "w").write("%s. %s\n%s%s\ndt_open\n%s\ndt_run\ndt_close\n"
                          % (load(MOD), WM, (". %s\n" % LOOK) if look else "",
                             pre, WR))
    e = {"DT_BLITCAT": "/nonexistent"}
    e.update(env or {})
    t = Term(path, env=e, rows=ROWS, cols=COLS, settle=0.5)
    if not t.idle:
        t.idle = t.until_idle(8 * (4 if os.environ.get("ASAN_OPTIONS") else 1))
    t.quit(b"qy", 1.2)
    os.unlink(path)
    return t.screen()


def cells(sc, r, c, n):
    return [sc.at(r, c + i) for i in range(n)]


ROLES = {"DT_TRACK": "#405060", "DT_TRACKFILL": "#102030", "DT_KNOB": "#708090",
         "DT_BUTTONFACE": "#203040", "DT_BUTTONLIGHT": "#e0e8f0",
         "DT_BUTTONDARK": "#080a0c"}

# Unicode, the roles given.
sc = run(ROLES)
at = sc.find("@")
r, c = at if at else (0, 0)
fg = [sc.style(r, c + 1 + i)["fg"] for i in range(10)]
check("a slider is its left end and the track filled to the knob in trackfill, "
      "the knob in knob, the track ahead and its right end in track",
      at is not None and "".join(cells(sc, r, c + 1, 10)) == "━━━━●─────" and
      fg == ["#102030"] * 4 + ["#708090"] + ["#405060"] * 5,
      (cells(sc, r, c + 1, 10), fg))
col = [sc.at(r + i, c + 13) for i in range(6)]
cfg = [sc.style(r + i, c + 13)["fg"] for i in range(6)]
check("a fader is the same stood on end, min at the bottom: track above the "
      "knob, filled below it",
      "".join(col) == "│││●┃┃" and
      cfg == ["#405060"] * 3 + ["#708090"] + ["#102030"] * 2, (col, cfg))
check("and dt_fadepos is its inverse: the top row is max, the bottom min, a "
      "quarter down the third row between",
      sc.find("fp 100 0 65 ") is not None, sc)
k = r + 2
raised = [sc.style(k, c + 1 + i) for i in range(3)]
check("an icon button is a key cap in the button roles: face, the icon in the "
      "light edge's colour, and no bevel where the glyph set has none",
      sc.at(k, c + 2) == "▶" and sc.at(k, c + 1) == " " and sc.at(k, c + 3) == " " and
      all(s["bg"] == "#203040" and s["fg"] == "#e0e8f0" for s in raised) and
      not any(x == "51" or x.startswith("51:") for i in range(12)
              for x in sc.p[k][c + 1 + i].split(";")),
      [sc.p[k][c + 1 + i] for i in range(12)])
check("and one that is down takes its dark edge for a face; a lit one is down "
      "and filled with the accent, its icon in the selection's ink",
      sc.style(k, c + 6)["bg"] == "#080a0c" and sc.style(k, c + 6)["fg"] == "#e0e8f0" and
      sc.style(k, c + 10)["bg"] == "#63b3ed" and sc.style(k, c + 10)["fg"] == "#1a202c",
      (sc.style(k, c + 6), sc.style(k, c + 10)))

# Unicode, the roles left out.
sc = run()
at = sc.find("@")
r, c = at if at else (0, 0)
fg = [sc.style(r, c + 1 + i)["fg"] for i in range(10)]
check("left out, the track is the dim colour, the fill the accent, the knob "
      "the ink", fg == ["#63b3ed"] * 4 + ["#cbd5e0"] + ["#718096"] * 5, fg)
s = sc.style(r + 2, c + 2)
check("and a key on a dark scheme is the well, lit by the ink",
      s["bg"] == "#2d3748" and s["fg"] == "#cbd5e0", s)
sc = run({"DT_FACE": "#f5f2ea", "DT_INK": "#2d3748", "DT_WELL": "#e2ddd0"})
at = sc.find("@")
r, c = at if at else (0, 0)
s = sc.style(r + 2, c + 2)


def lum(h):
    return 0 if not h else (2126 * int(h[1:3], 16) + 7152 * int(h[3:5], 16) +
                            722 * int(h[5:7], 16)) // 10000


check("on a light scheme it is the face deepened to a grey the face's own "
      "colour reads on",
      s["fg"] == "#f5f2ea" and s["bg"] is not None and lum(s["bg"]) < 160, s)

# The blit glyph set: Blit's pieces, from the tree's table.
sc = run(dict(ROLES, DT_GLYPHSET="blit"))
at = sc.find("@")
r, c = at if at else (0, 0)
want = ([bg("slider-cap-left")] + [bg("slider-track-filled")] * 3 +
        [bg("slider-knob")] + [bg("slider-track")] * 4 + [bg("slider-cap-right")])
check("in the blit set a slider is Blit's own pieces, end to end",
      cells(sc, r, c + 1, 10) == want, [hex(ord(x[0])) if x else x for x in cells(sc, r, c + 1, 10)])
wantf = ([bg("fader-cap-top")] + [bg("fader-track")] * 2 + [bg("fader-knob")] +
         [bg("fader-track-filled"), bg("fader-cap-bottom")])
check("and a fader its fader pieces, top to bottom",
      [sc.at(r + i, c + 13) for i in range(6)] == wantf,
      [hex(ord(sc.at(r + i, c + 13)[0])) for i in range(6)])
k = r + 2
p = sc.p[k]
check("an icon button is Blit's key: key-left, its icon framed raised (51:6) "
      "with the dark edge as SGR 58, key-right",
      sc.at(k, c + 1) == bg("key-left") and sc.at(k, c + 3) == bg("key-right") and
      "51:6" in p[c + 2] and "58:2::8:10:12" in p[c + 2] and
      sc.style(k, c + 2)["bg"] == "#203040" and sc.style(k, c + 2)["fg"] == "#e0e8f0",
      (p[c + 1], p[c + 2], p[c + 3]))
check("pressed, its pressed ends and the icon framed 51:7, its face its own; "
      "lit, the same and filled with the accent",
      sc.at(k, c + 5) == bg("key-pressed-left") and sc.at(k, c + 7) == bg("key-pressed-right") and
      "51:7" in p[c + 6] and sc.style(k, c + 6)["bg"] == "#203040" and
      sc.at(k, c + 9) == bg("key-pressed-left") and "51:7" in p[c + 10] and
      sc.style(k, c + 10)["bg"] == "#63b3ed",
      (p[c + 5], p[c + 6], p[c + 10]))

# Left out on Blit's pieces, the track is the filled part's colour: Blit
# draws it at 38% itself, and the dim colour drawn that faint all but went.
sc = run({"DT_GLYPHSET": "blit"})
at = sc.find("@")
r, c = at if at else (0, 0)
fg = [sc.style(r, c + 1 + i)["fg"] for i in range(10)]
check("left out on Blit's pieces, the track ahead is the accent too, which "
      "Blit draws faint", fg == ["#63b3ed"] * 4 + ["#cbd5e0"] + ["#63b3ed"] * 5, fg)

# A settings file written before the LCD roles takes them from its scheme.
conf = scratch("widgetroles.json")
json.dump({"settings": {"CP_COLOURS": "amber", "DT_FACE": "#0f0900"}}, open(conf, "w"))
sc = run({"DT_CONF": conf}, look=True)
check("a settings file written before the LCD roles takes them from the scheme "
      "it names", sc.find("lcd #050300 #ffb000 track []") is not None, sc)
os.unlink(conf)
conf = scratch("widgetroles.json")
sc = run({"DT_CONF": conf}, pre="cp_colours paper\n", look=True)
check("and applying a scheme sets them", sc.find("lcd #0b0c0a #c6f26a") is not None, sc)
try:
    os.unlink(conf)
except OSError:
    pass

# Slider Style (DT_SLIDERSTYLE): each style a family of the parts' glyphs,
# chosen through dt_set as the Control Panel row does, each drawn as the glyph
# set draws it -- the unicode set's own, and Blit's family by id from the
# tree's table. Classic is what every check above already drew.
UNI = {"circle": ("╺━━━◉────╴", "╷││◉┃╹"),
       "groove": ("╞═══◆────┤", "┬││◆║╨")}


def gone(path):
    try:
        os.unlink(path)
    except OSError:
        pass


def styled(sc):
    at = sc.find("@")
    r, c = at if at else (0, 0)
    return ("".join(cells(sc, r, c + 1, 10)),
            "".join(sc.at(r + i, c + 13) for i in range(6)),
            [sc.style(r, c + 1 + i)["fg"] for i in range(10)])


for st, (s, f) in UNI.items():
    conf = scratch("widgetroles-style.json")
    got = styled(run(dict(ROLES, DT_CONF=conf), pre="dt_set DT_SLIDERSTYLE %s\n" % st))
    check("Slider Style %s draws its own family in the unicode set, a slider and "
          "a fader, the track ahead still in track" % st,
          got == (s, f, ["#102030"] * 4 + ["#708090"] + ["#405060"] * 5), got)
    gone(conf)
for st in ("circle", "groove"):
    conf = scratch("widgetroles-style.json")
    got = styled(run(dict(ROLES, DT_GLYPHSET="blit", DT_CONF=conf),
                     pre="dt_set DT_SLIDERSTYLE %s\n" % st))
    s = "".join([bg("slider-%s-cap-left" % st)] + [bg("slider-%s-track-filled" % st)] * 3 +
                [bg("slider-%s-knob" % st)] + [bg("slider-%s-track" % st)] * 4 +
                [bg("slider-%s-cap-right" % st)])
    f = "".join([bg("fader-%s-cap-top" % st)] + [bg("fader-%s-track" % st)] * 2 +
                [bg("fader-%s-knob" % st)] + [bg("fader-%s-track-filled" % st),
                                              bg("fader-%s-cap-bottom" % st)])
    check("Slider Style %s in the blit set is Blit's %s family, slider and fader"
          % (st, st), got[:2] == (s, f),
          [[hex(ord(x)) for x in y] for y in got[:2]])
    gone(conf)

# Kept: chosen in one session, the next one draws it, the glyph set unmoved.
conf = scratch("widgetroles-style.json")
run({"DT_CONF": conf}, pre="dt_set DT_SLIDERSTYLE groove\n")
got = styled(run({"DT_CONF": conf}))
check("the Slider Style chosen is kept: the next session draws groove",
      got[:2] == UNI["groove"], got)
gone(conf)
report(21)
