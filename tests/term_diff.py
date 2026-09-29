#!/usr/bin/env python3
"""The terminal emulator, checked against another one.

mods/term is fed the same bytes as a private tmux server, and the screens
they end up with are compared cell by cell: text always, attributes where a
case asks for it. tmux is the reference because it is a complete, widely
used emulator that can be run headless and read back exactly; where it and
xterm disagree, xterm wins -- xterm-256color is the terminfo entry programs
on hibr's terminal are given -- and the case says so in HIBR_ONLY with the
reason, and asserts xterm's behaviour directly instead.

Three kinds of input:
  - short synthetic sequences, each aimed at one behaviour;
  - real programs' output, recorded once into tests/term/*.bin from fixed
    input (a generated file, never this machine's own process list or
    anything else about it) -- re-record with --record;
  - checks tmux cannot make, in hibr_only(): a sequence split across two
    reads, and the replies a program's queries get.

Run directly:  python3 tests/term_diff.py [--record] [path-to-hibr]
Skips, rather than fails, when tmux is not installed.
"""
import os, re, shutil, subprocess, sys, tempfile, time, unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen
from screen import check, report, tree

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "term")
MODS = tree("build/mods")
E = "\x1b"

args = [a for a in sys.argv[1:] if a != "--record"]
if args:
    screen.HIBR = os.path.abspath(args[0])
RECORD = "--record" in sys.argv

# name: (rows, cols, bytes, compare attributes too)
CASES = {
    "plain":        (6, 20, "hello\r\nworld", False),
    "rep":          (4, 20, "ab" + E + "[3b|", False),
    "combining":    (4, 20, "éX" + E + "[1;3H|", False),
    "combining2":   (4, 20, "à́b", False),
    "kitty_query":  (8, 20, E + "[5;5H" + E + "7" + E + "[1;1HAB" + E + "[?uCD", False),
    "kitty_push":   (8, 20, E + "[5;5H" + E + "7" + E + "[1;1HAB" + E + "[>1uCD", False),
    "kitty_pop":    (8, 20, E + "[5;5H" + E + "7" + E + "[1;1HAB" + E + "[<uCD", False),
    "modkeys":      (4, 20, "a" + E + "[>4;2mb" + E + "[>4mc", True),
    "dcs":          (4, 20, "A" + E + "P+q544e" + E + "\\B", False),
    "apc":          (4, 20, "A" + E + "_Gf=24;xyz" + E + "\\B", False),
    "pm_sos":       (4, 20, "A" + E + "^pm" + E + "\\B" + E + "Xsos" + E + "\\C", False),
    "osc_bel":      (4, 20, "A" + E + "]0;title\x07B", False),
    "osc_st":       (4, 20, "A" + E + "]2;title" + E + "\\B", False),
    "cbt":          (4, 30, E + "[1;20HX" + E + "[ZY", False),
    "wide":         (4, 20, "漢字ab", False),
    "wide_edge":    (4, 5, "abcd漢", False),
    "dch_wide":     (4, 20, "a漢b" + E + "[1;2H" + E + "[P|", False),
    "cuu_margin":   (8, 20, E + "[3;6r" + E + "[4;1H" + E + "[5AX", False),
    "cud_margin":   (8, 20, E + "[3;6r" + E + "[4;1H" + E + "[9BX", False),
    "origin":       (8, 20, E + "[3;6r" + E + "[?6h" + E + "[1;1HX" + E + "[?6l" + E + "[r", False),
    "tabstops":     (4, 30, E + "[3g" + E + "[1;5H" + E + "H\r\tX", False),
    "c0_in_csi":    (6, 20, E + "[2\n;5HX", False),
    "can_abort":    (4, 20, E + "[31\x18X", False),
    "sub_abort":    (4, 20, E + "[31\x1aX", False),
    "decsc_pen":    (4, 20, E + "[1m" + E + "7" + E + "[0mA" + E + "8B", True),
    "decsc_wrap":   (6, 40, "0123456789" * 4 + E + "7" + E + "[5;1H" + E + "8Q", False),
    "irm":          (4, 20, "abc" + E + "[1;2H" + E + "[4hX" + E + "[4l", False),
    "irm_off":      (4, 20, "abc" + E + "[1;2H" + E + "[4h" + E + "[4lX", False),
    "scroll_region": (8, 20, E + "[2;4r" + E + "[2;1H1\r\n2\r\n3\r\n4\r\n5" + E + "[r", False),
    "ri_top":       (6, 20, "a\r\nb" + E + "[1;1H" + E + "MX", False),
    "su_sd":        (6, 20, "1\r\n2\r\n3\r\n4" + E + "[2S" + E + "[1T", False),
    "ech":          (4, 20, "abcdef" + E + "[1;2H" + E + "[3X", False),
    "el_modes":     (4, 20, "abcdef" + E + "[1;3H" + E + "[1K\r\n" + "ghij" + E + "[2;2H" + E + "[K", False),
    "ed_modes":     (6, 20, "aaa\r\nbbb\r\nccc" + E + "[2;2H" + E + "[1J", False),
    "decaln":       (3, 5, E + "#8", False),
    "sgr_basic":    (4, 30, E + "[1mB" + E + "[22;3mI" + E + "[23;4mU" + E + "[24;7mR" + E + "[27;9mS" + E + "[0mn", True),
    "sgr_colours":  (4, 30, E + "[31mr" + E + "[42mg" + E + "[91mR" + E + "[103mY" + E + "[39;49mn", True),
    "sgr_256":      (4, 30, E + "[38;5;196ma" + E + "[48;5;21mb" + E + "[0mc", True),
    "sgr_rgb":      (4, 30, E + "[38;2;10;20;30ma" + E + "[48;2;200;100;50mb" + E + "[0mc", True),
    "sgr_colon":    (4, 30, E + "[38:5:196ma" + E + "[38:2::10:20:30mb" + E + "[0mc", True),
    "sgr_ul_colon": (4, 30, E + "[4:3mc" + E + "[4:0mn", True),
    "sgr_ulcolour": (4, 30, E + "[58;5;196ma" + E + "[58;2;1;2;3mb" + E + "[59mc", True),
    "sgr_hidden":   (4, 30, "a" + E + "[8mb" + E + "[28mc", False),
    "bce":          (4, 20, E + "[44m" + E + "[2J" + E + "[0mx", False),
    "sync":         (4, 20, E + "[?2026hsync" + E + "[?2026l", False),
}

# Where xterm and tmux disagree and hibr follows xterm: the case, what
# hibr's own row 0 must be instead, and why.
HIBR_ONLY = {
    "el_wrap": (6, 40, "0123456789" * 4 + E + "[KZ", 0,
                "0123456789" * 3 + "012345678Z",
                "an erase includes the cursor's cell while a wrap is pending, "
                "and ends the pending wrap (DEC STD 070, xterm); tmux keeps "
                "its cursor past the edge and erases nothing"),
    "rep_wrap": (4, 6, "x" + E + "[9b", 1, "xxxx",
                 "REP prints its character n more times, wrapping as printing "
                 "does (xterm); tmux stops at the edge"),
    "decgfx": (4, 20, E + "(0lqqk" + E + "(B ok", 0, "┌──┐ ok",
               "DEC Special Graphics draws lines, as every VT does; tmux's "
               "plain capture shows the letters it stores them as"),
    "decgfx_so": (4, 20, E + ")0\x0elqk\x0f plain", 0, "┌─┐ plain",
                  "SO shifts G1 in, SI shifts it back out (xterm)"),
    "cht": (4, 30, "a" + E + "[2Ib", 0, "a" + " " * 15 + "b",
            "CHT moves forward n tab stops (ECMA-48); tmux 3.3a ignores it"),
    "il_col": (6, 20, E + "[2;5HAB" + E + "[2;5H" + E + "[LX", 1, "X",
               "IL leaves the cursor at line home (ECMA-48 8.3.67, VTE); "
               "tmux keeps the column"),
    "dl_col": (6, 20, "one\r\ntwo\r\nthree" + E + "[2;3H" + E + "[MX", 1,
               "Xhree", "DL leaves the cursor at line home, as IL does"),
    "wide_half": (4, 20, "漢" + E + "[1;2Hx|", 0, " x|",
                  "writing over either half of a wide character blanks the "
                  "other half (xterm, VTE); tmux moves the write along"),
    "ich_wide": (4, 20, "漢ab" + E + "[1;2H" + E + "[@|", 0, " | ab",
                 "inserting inside a wide character splits it, and a split "
                 "wide character is blank (xterm, VTE)"),
    "utf8_bad": (4, 20, "a\xff\xfeb", 0, "a\ufffd\ufffdb",
                 "an invalid byte shows as U+FFFD (xterm, VTE); tmux drops it"),
}

# Programs recorded into tests/term/<name>.bin by --record, at 24x80.
PROGRAMS = {
    "less":     (["less", "-R", "{file}"], "  q", 1.5),
    "nano":     (["nano", "--ignorercfiles", "{file}"], "\x18", 1.5),
    "whiptail": (["whiptail", "--title", "Box", "--msgbox", "Line drawing, "
                  "shadows and buttons from newt.", "12", "50"], "", 1.5),
    "vi":       (["vi", "{file}"], ":q!\r", 1.5),
    "screen":   (["screen", "-q", "-c", "/dev/null", "sh", "-c",
                  "cat {file}; sleep 1"], "", 2.0),
}


def text_file(path):
    lines = ["hibr terminal emulator fixture"]
    for i in range(1, 60):
        lines.append("%03d %s" % (i, "lorem ipsum dolor sit amet " * (i % 4 + 1)))
    lines.append("unicode: é à ü ñ 漢字 ─│┌┐ ✓")
    open(path, "w").write("\n".join(lines) + "\n")


def record():
    """Record each program's output into tests/term/, from fixed input."""
    import pty, select, fcntl, termios, struct
    os.makedirs(FIX, exist_ok=True)
    d = tempfile.mkdtemp(prefix="hibr-rec-")
    f = os.path.join(d, "fixture.txt")
    text_file(f)
    for name, (argv, keys, secs) in PROGRAMS.items():
        if not shutil.which(argv[0]):
            print("skip", name)
            continue
        argv = [a.replace("{file}", f) for a in argv]
        pid, fd = pty.fork()
        if pid == 0:
            env = {"TERM": "xterm-256color", "LANG": "C.UTF-8",
                   "HOME": d, "PATH": os.environ["PATH"], "LESS": "",
                   "SCREENDIR": d}
            os.execvpe(argv[0], argv, env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        buf = b""
        end = time.time() + secs
        while time.time() < end:
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    buf += os.read(fd, 65536)
                except OSError:
                    break
        if keys:
            os.write(fd, keys.encode())
            end = time.time() + 0.8
            while time.time() < end:
                if select.select([fd], [], [], 0.05)[0]:
                    try:
                        buf += os.read(fd, 65536)
                    except OSError:
                        break
        try:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except OSError:
            pass
        open(os.path.join(FIX, name + ".bin"), "wb").write(buf)
        print("recorded", name, len(buf))
    shutil.rmtree(d, True)


def hibr_screen(rows, cols, data, attrs):
    """Rows of text, and optionally of cells, from hibr's own emulator."""
    d = tempfile.mkdtemp(prefix="hibr-td-")
    f = os.path.join(d, "in.bin")
    open(f, "wb").write(data)
    script = ("mod load %s/pty.so; mod load %s/term.so\n"
              "t := term new -r %d -c %d\nterm feed $t -f %s\n"
              "r=0\nwhile [ $r -lt %d ]; do\n"
              "  x := term row $t $r; echo \"R|$x\"\n"
              "  %s\n"
              "  r=$((r + 1))\ndone\n"
              % (MODS, MODS, rows, cols, f, rows,
                 'y := term cells $t $r; echo "C|$y"; echo "E|"' if attrs else ":"))
    p = subprocess.run([screen.HIBR, "-c", script], capture_output=True,
                       text=True)
    shutil.rmtree(d, True)
    text, cells, cur = [], [], None
    for ln in p.stdout.split("\n"):
        if ln.startswith("R|"):
            text.append(ln[2:])
            cur = []
        elif ln.startswith("C|") or (cur is not None and not ln.startswith("E|")
                                     and ln and attrs):
            body = ln[2:] if ln.startswith("C|") else ln
            if body:
                cur.append(body.split("\t", 4))
        elif ln.startswith("E|"):
            cells.append(cur)
            cur = None
    return text, cells, p.stderr


SOCK = "hibr-td-%d" % os.getpid()


def tmux_screen(rows, cols, data, attrs):
    """Rows of text, and optionally of cells, from a private tmux server."""
    d = tempfile.mkdtemp(prefix="hibr-tt-")
    f = os.path.join(d, "in.bin")
    done = os.path.join(d, "done")
    conf = os.path.join(d, "tmux.conf")
    open(f, "wb").write(data)
    open(conf, "w").write("set -g status off\nset -g default-terminal xterm-256color\n")
    cmd = "stty -opost -onlcr; cat %s; touch %s; sleep 30" % (f, done)
    subprocess.run(["tmux", "-L", SOCK, "-f", conf, "new-session", "-d",
                    "-x", str(cols), "-y", str(rows), "-s", "s", cmd],
                   check=True)
    end = time.time() + 5
    while time.time() < end and not os.path.exists(done):
        time.sleep(0.02)
    time.sleep(0.15)
    cap = subprocess.run(["tmux", "-L", SOCK, "capture-pane", "-p", "-t", "s"]
                         + (["-e"] if attrs else []),
                         capture_output=True, text=True).stdout
    subprocess.run(["tmux", "-L", SOCK, "kill-server"], capture_output=True)
    shutil.rmtree(d, True)
    lines = cap.split("\n")[:rows]
    lines += [""] * (rows - len(lines))
    if not attrs:
        return [l.rstrip(" ") for l in lines], None
    text, cells = [], []
    for l in lines:
        t, c = parse_sgr_line(l)
        text.append(t.rstrip(" "))
        cells.append(c)
    return text, cells


def parse_sgr_line(line):
    """tmux's capture-pane -e line into its text and its cells, in the
    same names term cells uses."""
    pen = {"fg": "d", "bg": "d", "a": set()}
    cells, text, col = [], "", 0
    i = 0
    while i < len(line):
        m = re.match(r"\x1b\[([0-9;:]*)m", line[i:])
        if m:
            sgr(pen, m.group(1))
            i += len(m.group(0))
            continue
        ch = line[i]
        i += 1
        if unicodedata.combining(ch) and cells:
            cells[-1][4] += ch
            text += ch
            continue
        w = 2 if unicodedata.east_asian_width(ch) in "WF" else 1
        at = "".join(x for x in "bdiukrsh" if x in pen["a"]) or "-"
        cells.append([str(col), pen["fg"], pen["bg"], at, ch])
        text += ch
        col += w
    return text, cells


def sgr(pen, s):
    ps = [p for p in re.split(r"[;]", s)] if s else ["0"]
    i = 0
    while i < len(ps):
        p = ps[i]
        sub = p.split(":")
        v = int(sub[0] or 0)
        if v == 0:
            pen["fg"], pen["bg"], pen["a"] = "d", "d", set()
        elif v in (1, 2, 3, 5, 7, 8, 9):
            pen["a"].add({1: "b", 2: "d", 3: "i", 5: "k", 7: "r", 8: "h", 9: "s"}[v])
        elif v == 4:
            if len(sub) > 1 and sub[1] == "0":
                pen["a"].discard("u")
            else:
                pen["a"].add("u")
        elif v == 22:
            pen["a"] -= {"b", "d"}
        elif v == 23:
            pen["a"].discard("i")
        elif v == 24:
            pen["a"].discard("u")
        elif v == 27:
            pen["a"].discard("r")
        elif v == 29:
            pen["a"].discard("s")
        elif 30 <= v <= 37:
            pen["fg"] = "p%d" % (v - 30)
        elif 40 <= v <= 47:
            pen["bg"] = "p%d" % (v - 40)
        elif 90 <= v <= 97:
            pen["fg"] = "p%d" % (v - 90 + 8)
        elif 100 <= v <= 107:
            pen["bg"] = "p%d" % (v - 100 + 8)
        elif v == 39:
            pen["fg"] = "d"
        elif v == 49:
            pen["bg"] = "d"
        elif v in (38, 48, 58):
            if len(sub) > 1:
                vals = sub[1:]
            else:
                vals = ps[i + 1:]
            k = vals[0] if vals else ""
            if k == "5":
                c = "p%d" % int(vals[1])
                used = 2
            elif k == "2":
                rgb = [x for x in vals[1:4]]
                c = "#%02x%02x%02x" % tuple(int(x or 0) for x in rgb)
                used = 4
            else:
                c, used = "d", 1
            if len(sub) == 1:
                i += used
            if v == 38:
                pen["fg"] = c
            elif v == 48:
                pen["bg"] = c
        i += 1


def compare(name, rows, cols, data, attrs):
    ht, hc, err = hibr_screen(rows, cols, data, attrs)
    tt, tc = tmux_screen(rows, cols, data, attrs)
    ok = ht == tt
    detail = ""
    if not ok:
        for r in range(rows):
            if r >= len(ht) or ht[r] != tt[r]:
                detail = "row %d: hibr %r, tmux %r" % (
                    r, ht[r] if r < len(ht) else None, tt[r])
                break
        if err:
            detail += " (stderr: %s)" % err.strip()[:200]
    if ok and attrs:
        for r in range(rows):
            hk = [c for c in hc[r] if c[4].strip()] if r < len(hc) else []
            tk = [c for c in tc[r] if c[4].strip()]
            if hk != tk:
                ok = False
                detail = "row %d cells: hibr %r, tmux %r" % (r, hk[:6], tk[:6])
                break
    check("%s matches tmux" % name, ok, detail or None)


def hibr_only():
    """What tmux cannot be compared on."""
    for name, (rows, cols, s, row, want, why) in HIBR_ONLY.items():
        data = s.encode("latin1") if "\xff" in s else s.encode()
        ht, _, _ = hibr_screen(rows, cols, data, False)
        check("%s: %s" % (name, why.split(";")[0]), ht[row] == want,
              "row %d: %r, want %r" % (row, ht[row], want))

    # A sequence split across two reads must mean what it means whole.
    script = ("mod load %s/pty.so; mod load %s/term.so\n"
              "t := term new -r 4 -c 20\n"
              "term feed $t a $'\\e' '[31mb'\n"
              "term feed $t $'\\e]0;ti' $'tle\\e' '\\c'\n"
              "x := term row $t 0; echo \"$x\"\n"
              "y := term cells $t 0; echo \"$y\"\n"
              "z := term title $t; echo \"T:$z\"\n" % (MODS, MODS))
    p = subprocess.run([screen.HIBR, "-c", script], capture_output=True,
                       text=True)
    out = p.stdout
    check("a CSI split between two reads still colours what follows it",
          out.startswith("abc\n") and "\tp1\td\t-\tb" in out, out + p.stderr)
    check("an OSC whose ST is split between two reads leaves no backslash",
          "T:title" in out and "\\" not in out.split("\n")[0], out)

    # A background-colour query (OSC 11) is answered with the colours the
    # emulator was given, and not at all without them -- through a real
    # pty, since a reply goes to the program.
    d = tempfile.mkdtemp(prefix="hibr-osc-")
    q = os.path.join(d, "q.sh")
    open(q, "w").write("stty raw -echo\nsleep 0.3\n"
                       "printf '\\033]11;?\\033\\\\'\n"
                       "r=$(dd bs=1 count=25 2>/dev/null)\n"
                       "printf 'got:%s' \"$(printf %s \"$r\" | tr '\\033\\\\' EB)\"\n"
                       "sleep 0.5\n")
    for setting, want in (("'#cbd5e0' '#101820'", "got:E]11;rgb:1010/1818/2020EB"),
                          ("off", "")):
        script = ("mod load %s/pty.so; mod load %s/term.so\n"
                  "t := term open -r 4 -c 70 /bin/sh %s\n"
                  "term colors $t %s\n"
                  "i=0; while [ $i -lt 30 ]; do term poll $t 50; i=$((i + 1)); done\n"
                  "x := term row $t 0; echo \"ROW[$x]\"\nterm close $t\n"
                  % (MODS, MODS, q, setting))
        out = subprocess.run([screen.HIBR, "-c", script], capture_output=True,
                             text=True).stdout
        check("OSC 11 %s" % ("is answered with the colours given" if want
                             else "goes unanswered with none given"),
              "ROW[%s]" % want in out, out)
    shutil.rmtree(d, True)

    # A program on this terminal is given a UTF-8 locale when the one it
    # would inherit is not -- the C locale, or a UTF-8 name this machine
    # does not have, which fails to load and leaves it in C just the same.
    # Either way ls would print a UTF-8 name as escaped bytes.
    d = tempfile.mkdtemp(prefix="hibr-loc-")
    open(os.path.join(d, "café-漢.txt"), "w").close()
    for lang in ("C", "xx_XX.UTF-8"):
        script = ("mod load %s/pty.so; mod load %s/term.so\n"
                  "export LANG=%s; unset LC_ALL LC_CTYPE\n"
                  "t := term open -r 3 -c 60 ls %s\n"
                  "i=0; while [ $i -lt 10 ]; do term poll $t 50; i=$((i + 1)); done\n"
                  "x := term row $t 0; echo \"ROW[$x]\"; echo \"LANG[$LANG]\"\n"
                  "term close $t\n" % (MODS, MODS, lang, d))
        out = subprocess.run([screen.HIBR, "-c", script], capture_output=True,
                             text=True).stdout
        check("under LANG=%s a program still gets UTF-8, and the desktop's own "
              "LANG is left alone" % lang,
              "ROW[café-漢.txt]" in out and "LANG[%s]" % lang in out, out)
    shutil.rmtree(d, True)


if RECORD:
    record()
    sys.exit(0)

if not shutil.which("tmux"):
    print("tmux not installed: nothing to compare against")
    sys.exit(0)

for name, (rows, cols, s, attrs) in CASES.items():
    compare(name, rows, cols, s.encode("utf-8", "surrogateescape")
            if "\xff" not in s else s.encode("latin1"), attrs)

ran = len(CASES)
for name in sorted(PROGRAMS):
    f = os.path.join(FIX, name + ".bin")
    if os.path.exists(f):
        compare("recorded " + name, 24, 80, open(f, "rb").read(), False)
        ran += 1

hibr_only()
report(ran + len(HIBR_ONLY) + 6)
