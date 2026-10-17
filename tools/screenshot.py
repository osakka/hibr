#!/usr/bin/env python3
"""Render a real hibr session's screen to a PNG, pixel for pixel true to what
the terminal actually drew -- the characters, and the pictures.

This is not a mockup or a hand-drawn picture: it runs hibr on a real pty,
the same way tests/screen.py does, and reconstructs the exact character grid
-- text, foreground, background, bold, dim -- from the real escape sequences
the console module sent. Every screenshot this produces is provably a real
render of real output, which is the whole point: a picture in the docs
cannot silently drift from what hibr actually draws once it was made this
way, and cannot show something hibr does not actually do.

A **bitmap** is drawn the same way and from the same evidence. A kitty
transmission is kept whole, base64-decoded, inflated when the control says
`o=z`, scaled to the cells the placement names, and pasted under the text;
a cell with no background colour of its own lets it through, which is the
contract the wallpaper itself depends on (ADR 0037). Until 0.99.148 this
could not show a picture at all, and worse: its escape skip advanced two
bytes whatever the escape was, so an APC payload -- tens of thousands of
base64 characters -- was fed to the grid as text, and the harness's own
idle marker (an OSC) was drawn across whatever window the cursor was in
(Gitea #181). Pass --cell to say what a cell measures, which is what makes
pixels possible at all; without it the terminal reports no cell size and
hibr draws half blocks, exactly as on a terminal that cannot place pixels.

Usage:
    tools/screenshot.py out.png 'dt_new "Hello" 8 30 6 10'
    tools/screenshot.py out.png 'dt_new "Calc" 16 24 2 2 calc' --apps calc
    tools/screenshot.py out.png --session-file path/to/session.hibr
    tools/screenshot.py out.png 'dt_new "Hello" 8 30 6 10' --keys f10 --wait 0.6

Put the inline session before any --flag, not after: argparse's own
handling of an optional positional after an option can otherwise swallow
the session into the wrong argument, silently, rather than raising an
error. This is a limitation of this script's own argument parsing, not of
hibr.

The session positional is sourced after the window manager and console
module, then dt_open/dt_run/dt_close wrap it the same way tests/desktop.py
does. --session-file runs a session file directly instead (nothing is
wrapped around it). --apps is a comma-separated list of programs/desktop/apps/*.hibr
files to source first. --keys sends one key or one click before the
screenshot is taken; repeat --keys for more than one. click:row:col,
rclick:row:col, and mclick:row:col press the left, right, or middle button
at that position; wheelup:row:col and wheeldown:row:col scroll there; a
name in NAMED_KEYS below (f10, tab, enter, escape, the arrows, ctrl-c, and
a few more -- a small curated table, not every name the console module can
decode) sends that key's real bytes; anything else is sent as its own
literal text instead. --rows/--cols set the terminal size (default 24x80).
--wait sets how long to let it settle before reading the screen (default
0.6s).
"""
import argparse
import base64
import io
import os
import sys
import time
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "..", "tests"))
from screen import Term, load, tree, press, release, wheel  # noqa: E402

from PIL import Image, ImageDraw, ImageFont  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_BOLD_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
# Where a glyph DejaVu lacks is looked for, in order. A real terminal falls
# back to whatever font has the character; without this the picture drew
# the missing-glyph box for three Control Panel icons a terminal shows.
FALLBACK_PATHS = ["/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
                  "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf"]
CELL_W, CELL_H = 9, 18

DEFAULT_FG = (203, 213, 224)
DEFAULT_BG = (16, 24, 32)

# The 16 standard xterm colours, for the rare palette-indexed form
# (\033[38;5;n or 48;5;n); hibr's own themes are all truecolour, but a
# module or a program running inside a terminal window may still use this.
XTERM16 = [
    (0, 0, 0), (205, 0, 0), (0, 205, 0), (205, 205, 0),
    (0, 0, 238), (205, 0, 205), (0, 205, 205), (229, 229, 229),
    (127, 127, 127), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (92, 92, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]


def xterm256(n):
    if n < 16:
        return XTERM16[n]
    if n < 232:
        n -= 16
        levels = (0, 95, 135, 175, 215, 255)
        r, g, b = n // 36, (n // 6) % 6, n % 6
        return levels[r], levels[g], levels[b]
    v = 8 + (n - 232) * 10
    return v, v, v


class Cell:
    __slots__ = ("ch", "fg", "bg", "bold", "dim")

    def __init__(self):
        self.ch = " "
        self.fg = None
        self.bg = None
        self.bold = False
        self.dim = False


class ColourScreen:
    """Like tests/screen.py's Screen, but keeps colour and weight too -- and
    the pictures, which are not text and must never reach the grid."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.imgs = []
        self.clear()

    def clear(self):
        self.g = [[Cell() for _ in range(self.cols)] for _ in range(self.rows)]
        self.r = self.c = 0
        self.fg = self.bg = None
        self.bold = self.dim = False

    def _sgr(self, params):
        i = 0
        n = len(params)
        while i < n:
            p = params[i]
            if p in ("", "0"):
                self.fg = self.bg = None
                self.bold = self.dim = False
            elif p == "1":
                self.bold = True
            elif p == "2":
                self.dim = True
            elif p in ("22",):
                self.bold = self.dim = False
            elif p == "39":
                self.fg = None
            elif p == "49":
                self.bg = None
            elif p in ("38", "48"):
                bgp = p == "48"
                if i + 1 < n and params[i + 1] == "2" and i + 4 < n:
                    r, g, b = (int(params[i + 2]), int(params[i + 3]),
                               int(params[i + 4]))
                    if bgp:
                        self.bg = (r, g, b)
                    else:
                        self.fg = (r, g, b)
                    i += 4
                elif i + 1 < n and params[i + 1] == "5" and i + 2 < n:
                    col = xterm256(int(params[i + 2]))
                    if bgp:
                        self.bg = col
                    else:
                        self.fg = col
                    i += 2
            i += 1

    def feed(self, t):
        i, n = 0, len(t)
        while i < n:
            ch = t[i]
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "[":
                j = i + 2
                while j < n and not ("@" <= t[j] <= "~"):
                    j += 1
                if j >= n:
                    break
                seq, fin = t[i + 2:j], t[j]
                if fin == "H":
                    a = seq.split(";")
                    self.r = int(a[0]) - 1 if a and a[0].isdigit() else 0
                    self.c = (int(a[1]) - 1
                              if len(a) > 1 and a[1].isdigit() else 0)
                elif fin == "C":
                    self.c += int(seq) if seq.isdigit() else 1
                elif fin == "m":
                    self._sgr(seq.split(";") if seq else [""])
                elif fin == "J" and seq in ("2", "3"):
                    self.clear()
                i = j + 1
                continue
            # Everything below is an escape that is *not* text, and each
            # runs to its own terminator. Skipping two bytes instead -- what
            # this did until 0.99.148 -- leaves the rest of the string in
            # the grid: an APC's base64 scribbled across the screen, and the
            # harness's own `7777;idle;N;M` written over a window.
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "_":
                j = i + 2
                while j < n and t[j:j + 2] != "\x1b\\":
                    j += 1
                ctrl, _, data = t[i + 2:j].partition(";")
                if ctrl.startswith("Ga=T") or ctrl.startswith("Ga=p"):
                    self.imgs.append([self.r, self.c, ctrl, data])
                elif data and self.imgs:
                    # A picture larger than 4096 bytes arrives in chunks,
                    # and only the first carries the control.
                    self.imgs[-1][3] += data
                i = j + 2
                continue
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "]":
                j = i + 2
                while j < n and t[j] != "\x07" and t[j:j + 2] != "\x1b\\":
                    j += 1
                i = j + (1 if j < n and t[j] == "\x07" else 2)
                continue
            if ch == "\x1b" and i + 1 < n and t[i + 1] == "P":
                # A sixel. Its own bytes are not kept: sixel is paint, with
                # no placement to read a rectangle from, so there is nothing
                # faithful to draw. A screenshot of one shows the cells it
                # was painted over, and says so here rather than silently.
                j = i + 2
                while j < n and t[j:j + 2] != "\x1b\\":
                    j += 1
                i = j + 2
                continue
            if ch == "\x1b":
                i += 2
                continue
            if ch in "\r\n":
                i += 1
                continue
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                cell = self.g[self.r][self.c]
                cell.ch = ch
                cell.fg, cell.bg = self.fg, self.bg
                cell.bold, cell.dim = self.bold, self.dim
            self.c += 2 if ord(ch) > 0x2E80 else 1
            i += 1
        return self

    def pictures(self):
        """Each kept transmission as (row, col, image, z, cells): decoded,
        inflated if it was compressed, and with the cell rectangle its own
        placement named -- the payload is not one pixel per screen pixel,
        since the wallpaper is sent at DT_WALLDETAIL and the terminal scales
        it, so a faithful render has to scale it the same way."""
        out = []
        for r, c, ctrl, data in self.imgs:
            k = dict(p.split("=", 1) for p in ctrl[1:].split(",") if "=" in p)
            try:
                raw = base64.b64decode(data)
            except Exception:
                continue
            if k.get("o") == "z":
                try:
                    raw = zlib.decompress(raw)
                except Exception:
                    continue
            cells = (int(k["c"]), int(k["r"])) if "c" in k and "r" in k else None
            z = int(k.get("z", 0))
            if k.get("f") == "100":
                try:
                    out.append((r, c, Image.open(io.BytesIO(raw)).convert("RGB"), z, cells))
                except Exception:
                    pass
                continue
            w, h = int(k.get("s", 0)), int(k.get("v", 0))
            if not w or not h or len(raw) < w * h * 3:
                continue
            out.append((r, c, Image.frombytes("RGB", (w, h), raw[:w * h * 3]), z, cells))
        return out


def missing(font, ch, memo={}):
    """Whether a font draws its missing-glyph box for this character,
    found by comparing its mask with that of a code point no font has."""
    key = (font.path, ch)
    if key not in memo:
        nd = font.getmask("\U0010fffd")
        m = font.getmask(ch)
        memo[key] = m.size == nd.size and bytes(m) == bytes(nd)
    return memo[key]


def render(cs, path):
    font = ImageFont.truetype(FONT_PATH, 15)
    bold_font = ImageFont.truetype(FONT_BOLD_PATH, 15)
    falls = [ImageFont.truetype(p, 14) for p in FALLBACK_PATHS if os.path.exists(p)]
    img = Image.new("RGB", (cs.cols * CELL_W, cs.rows * CELL_H), DEFAULT_BG)
    # Pictures first, in z order, and the text over them: a cell with no
    # background colour of its own is left alone below, so the picture shows
    # through exactly where the desktop meant it to.
    for r, c, pic, z, cells in sorted(cs.pictures(), key=lambda p: p[3]):
        if cells:
            want = (cells[0] * CELL_W, cells[1] * CELL_H)
            if pic.size != want:
                pic = pic.resize(want, Image.LANCZOS)
        img.paste(pic, (c * CELL_W, r * CELL_H))
    draw = ImageDraw.Draw(img)
    for r in range(cs.rows):
        for c in range(cs.cols):
            cell = cs.g[r][c]
            bg = cell.bg or DEFAULT_BG
            x0, y0 = c * CELL_W, r * CELL_H
            if cell.bg is not None:
                draw.rectangle([x0, y0, x0 + CELL_W, y0 + CELL_H], fill=bg)
            if cell.ch != " ":
                fg = cell.fg or DEFAULT_FG
                if cell.dim:
                    fg = tuple(max(0, int(v * 0.6)) for v in fg)
                f = bold_font if cell.bold else font
                if ord(cell.ch) > 0x2000 and missing(f, cell.ch):
                    f = next((g for g in falls if not missing(g, cell.ch)), f)
                draw.text((x0, y0 - 2), cell.ch, font=f, fill=fg)
    img.save(path)


BUTTONS = {"click": 0, "mclick": 1, "rclick": 2}

# The named keys this recognises -- a small, curated table for the keys a
# screenshot actually needs, not every name the console module can decode.
# Anything not in here is sent as its own literal text instead.
NAMED_KEYS = {
    "f10": b"\x1b[21~", "alt-f4": b"\x1b[1;3S",
    "tab": b"\t", "shift-tab": b"\x1b[Z",
    "enter": b"\r", "escape": b"\x1b",
    "up": b"\x1b[A", "down": b"\x1b[B",
    "right": b"\x1b[C", "left": b"\x1b[D",
    "backspace": b"\x7f", "delete": b"\x1b[3~",
    "ctrl-c": b"\x03", "ctrl-\\": b"\x1c", "ctrl-z": b"\x1a",
}


def encode_key(k):
    """A --keys value: click:row:col (and mclick/rclick) sends a press and
    a release at that position, wheelup:row:col/wheeldown:row:col sends one
    scroll, a name from NAMED_KEYS sends that key's real bytes, and
    anything else is typed literally, byte for byte."""
    parts = k.split(":")
    if len(parts) == 3 and parts[0] in BUTTONS:
        r, c, b = int(parts[1]), int(parts[2]), BUTTONS[parts[0]]
        return press(r, c, b) + release(r, c, b)
    if len(parts) == 3 and parts[0] in ("wheelup", "wheeldown"):
        return wheel(int(parts[1]), int(parts[2]), up=parts[0] == "wheelup")
    if k in NAMED_KEYS:
        return NAMED_KEYS[k]
    return k.encode()


def appfile(name):
    """Where <name>.hibr is, searched the way the desktop itself scans.

    This used to be `programs/desktop/apps/<name>.hibr` and nothing else,
    which is flat where `dt_apps` globs `**` -- so every application in a
    submenu was unreachable. `--apps calc` silently sourced nothing, and
    the window opened with no app behind it: an empty box with a correct
    title, which reads as the app being broken rather than as never
    having been loaded. The desktop's own apps live a folder across
    again, so both roots are searched. A glob list is where coverage
    goes to hide, and this is one wearing a different hat."""
    for base in ("programs/desktop/apps", "programs/desktop/system"):
        root = os.path.join(ROOT, base)
        for d, _dirs, fs in os.walk(root):
            if name + ".hibr" in fs:
                return os.path.join(d, name + ".hibr")
    raise SystemExit("screenshot.py: no app called %s under "
                     "programs/desktop/apps or /system" % name)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="PNG path to write")
    ap.add_argument("session", nargs="?", default="",
                     help="inline session source, wrapped in dt_open/dt_run")
    ap.add_argument("--session-file", help="run this session file directly")
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--cols", type=int, default=80)
    ap.add_argument("--cell", default="",
                     help="what a cell measures in pixels, WxH -- a terminal "
                          "that says nothing here gets half blocks instead of "
                          "pixels, so a wallpaper needs this (try 9x18, which "
                          "is what this renderer draws)")
    ap.add_argument("--wait", type=float, default=0.6)
    ap.add_argument("--tick", default="60")
    ap.add_argument("--keys", action="append", default=[],
                     help="a key or raw bytes-as-text to send before the "
                          "screenshot; repeatable")
    ap.add_argument("--modules", default="console",
                     help="comma-separated modules to mod-load before "
                          "sourcing (default: console)")
    ap.add_argument("--apps", default="",
                     help="comma-separated app names to source first, found "
                          "anywhere under programs/desktop/apps or "
                          "programs/desktop/system")
    ap.add_argument("--until", default="",
                     help="wait for this text to appear before the shot -- "
                          "what a program in a window needs, since --wait is "
                          "a settle timeout and the desktop idles first")
    ap.add_argument("--untilmax", type=float, default=20.0,
                     help="how long --until may wait (default 20s)")
    ap.add_argument("--env", action="append", default=[],
                     help="NAME=VALUE for the session, repeatable -- a data "
                          "folder with apps placed in it, or a catalogue to "
                          "read, so a picture can show an install as it is")
    args = ap.parse_args()
    # nargs="*" here would greedily swallow the session positional that
    # follows on the command line -- a comma-separated string sidesteps the
    # ambiguity rather than requiring a careful argument order to avoid it.
    args.modules = [m for m in args.modules.split(",") if m]
    args.apps = [a for a in args.apps.split(",") if a]

    if args.session_file:
        path = args.session_file
        cleanup = False
    else:
        wm = tree("programs/desktop/desktop.hibr")
        apps_src = "".join(". %s\n" % appfile(a) for a in args.apps)
        content = ("%s. %s\n%s%s\ndt_open\n%s\ndt_run\ndt_close\n"
                   % (load(*args.modules), wm, apps_src, "", args.session))
        path = os.path.join(ROOT, "tools", ".screenshot-tmp.hibr")
        open(path, "w").write(content)
        cleanup = True

    cw, ch = 0, 0
    if args.cell:
        cw, ch = (int(v) for v in args.cell.lower().split("x", 1))
    env = {"DT_TICK": args.tick}
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k] = v
    if cw and ch:
        env["HIBR_GFX"] = "kitty"
    t = Term(path, env=env, rows=args.rows, cols=args.cols,
              settle=args.wait, cellw=cw, cellh=ch)
    # --until waits for the thing, because --wait cannot.
    #
    # `settle` is a *timeout* for the desktop to go idle, not a dwell: it
    # returns the moment the first idle marker arrives, which is long
    # before a program inside a window has started, let alone printed
    # anything. So a terminal window rendered as an empty box however
    # large --wait was made, and raising it did nothing at all -- the
    # desktop was already idle. Same rule as the suites' own
    # `Term.until`: wait for the text, not for the clock.
    if args.until:
        end = time.time() + args.untilmax
        while time.time() < end:
            t.collect(0.2)
            if args.until in t.text:
                break
        else:
            print("screenshot.py: %r never appeared in %gs"
                  % (args.until, args.untilmax), file=sys.stderr)
            sys.exit(1)
    # Keys after --until, not before: a key sent while what it acts on is
    # still loading lands on nothing, or on the wrong row once it loads.
    for k in args.keys:
        t.send(encode_key(k), settle=0.3)
    cs = ColourScreen(args.rows, args.cols)
    cs.feed(t.raw.decode("utf-8", errors="replace"))
    t.quit(b"qy", 1.0)
    if cleanup:
        os.unlink(path)

    render(cs, args.out)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
