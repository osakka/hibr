#!/usr/bin/env python3
"""Render a real hibr session's screen to a PNG, pixel for pixel true to what
the terminal actually drew.

This is not a mockup or a hand-drawn picture: it runs hibr on a real pty,
the same way tests/screen.py does, and reconstructs the exact character grid
-- text, foreground, background, bold, dim -- from the real escape sequences
the console module sent. Every screenshot this produces is provably a real
render of real output, which is the whole point: a picture in the docs
cannot silently drift from what hibr actually draws once it was made this
way, and cannot show something hibr does not actually do.

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
wrapped around it). --apps is a comma-separated list of examples/desktop/apps/*.hibr
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
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "..", "tests"))
from screen import Term, load, tree, press, release, wheel  # noqa: E402

from PIL import Image, ImageDraw, ImageFont  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_BOLD_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
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
    """Like tests/screen.py's Screen, but keeps colour and weight too."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
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


def render(cs, path):
    font = ImageFont.truetype(FONT_PATH, 15)
    bold_font = ImageFont.truetype(FONT_BOLD_PATH, 15)
    img = Image.new("RGB", (cs.cols * CELL_W, cs.rows * CELL_H), DEFAULT_BG)
    draw = ImageDraw.Draw(img)
    for r in range(cs.rows):
        for c in range(cs.cols):
            cell = cs.g[r][c]
            bg = cell.bg or DEFAULT_BG
            x0, y0 = c * CELL_W, r * CELL_H
            if bg != DEFAULT_BG:
                draw.rectangle([x0, y0, x0 + CELL_W, y0 + CELL_H], fill=bg)
            if cell.ch != " ":
                fg = cell.fg or DEFAULT_FG
                if cell.dim:
                    fg = tuple(max(0, int(v * 0.6)) for v in fg)
                f = bold_font if cell.bold else font
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


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="PNG path to write")
    ap.add_argument("session", nargs="?", default="",
                     help="inline session source, wrapped in dt_open/dt_run")
    ap.add_argument("--session-file", help="run this session file directly")
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--cols", type=int, default=80)
    ap.add_argument("--wait", type=float, default=0.6)
    ap.add_argument("--tick", default="60")
    ap.add_argument("--keys", action="append", default=[],
                     help="a key or raw bytes-as-text to send before the "
                          "screenshot; repeatable")
    ap.add_argument("--modules", default="console",
                     help="comma-separated modules to mod-load before "
                          "sourcing (default: console)")
    ap.add_argument("--apps", default="",
                     help="comma-separated examples/desktop/apps/<name>.hibr files "
                          "to source first")
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
        wm = tree("examples/desktop/desktop.hibr")
        apps_src = "".join(". %s/examples/desktop/apps/%s.hibr\n" % (ROOT, a)
                            for a in args.apps)
        content = ("%s. %s\n%s%s\ndt_open\n%s\ndt_run\ndt_close\n"
                   % (load(*args.modules), wm, apps_src, "", args.session))
        path = os.path.join(ROOT, "tools", ".screenshot-tmp.hibr")
        open(path, "w").write(content)
        cleanup = True

    t = Term(path, env={"DT_TICK": args.tick}, rows=args.rows, cols=args.cols,
              settle=args.wait)
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
