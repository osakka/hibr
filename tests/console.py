#!/usr/bin/env python3
"""Drive the console display through a pseudo terminal and read what it emits.

The .t files cannot reach any of this, for the same reason tests/editor.py
exists: full-screen drawing only happens when there is a terminal to draw on.
Run it directly:  python3 tests/console.py [path-to-hibr]

Unlike the other suites this one mostly asserts on the *bytes* rather than on
the reassembled screen, because what is being checked is that the damage model
sends the right escapes and not merely that the right picture comes out.
"""
import os, re, signal, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen as sx
from screen import Term, Screen, check, report, tree, expect

if len(sys.argv) > 1:
    sx.HIBR = os.path.abspath(sys.argv[1])
MOD = tree("build/mods/console.so")


def run(script, feed=(), after=None, wait=2.5, rows=24, cols=80, caps=None):
    """Run a script under a pty, optionally feeding keys, and return its output."""
    path = "/tmp/hibr-console-%d.hibr" % os.getpid()
    open(path, "w").write("mod load %s\n%s" % (MOD, script))
    t = Term(path, rows=rows, cols=cols, settle=0.4, caps=caps)
    t.keys(feed, settle=0.12, collect=0)
    if after:
        after(t)
    t.collect(wait)
    t.close()
    os.unlink(path)
    return t.raw, t.status


def counts(out):
    """The numbers the R line reports, one per flush the script measured."""
    m = re.search(rb"R ([-0-9 ]+)", out)
    return [int(x) for x in m.group(1).split()] if m else []


def drawn(out):
    """Everything emitted after the opening full paint."""
    i = out.rfind(b"\x1b[24;1H")
    return out[i:] if i >= 0 else out


o, _ = run('console open\nconsole flush\n'
           'console put 2 4 "hello screen"\nconsole flush\nconsole close\n')
check("the alternate screen is entered and left",
      b"\x1b[?1049h" in o and b"\x1b[?1049l" in o)
check("the cursor is hidden and given back",
      b"\x1b[?25l" in o and b"\x1b[?25h" in o)
check("text reaches the terminal in one piece", b"hello screen" in o)
check("it lands where it was put", b"\x1b[3;5Hhello screen" in drawn(o))

o, _ = run('console open\na := console flush\n'
           'console put 5 10 "0000"\nb := console flush\n'
           'console put 5 10 "0001"\nc := console flush\n'
           'd := console flush\nconsole close\necho "R $a $b $c $d"\n')
n = counts(o)
check("the first flush paints the whole screen", n and n[0] > 1900)
check("only the changed run is sent afterwards", len(n) > 1 and n[1] < 20)
check("one changed cell costs about one escape", len(n) > 2 and n[2] < 12)
check("an unchanged screen sends nothing at all", len(n) > 3 and n[3] == 0)

o, _ = run('console open\nconsole flush\n'
           'console put 1 0 "漢字ab"\nb := console flush\n'
           'console put 1 1 "X"\nc := console flush\nconsole close\necho "R $b $c"\n')
d = drawn(o)
check("a wide glyph is written whole", "漢字ab".encode() in d)
check("overwriting half of one clears the other half",
      re.search(rb"\x1b\[2;1H X", d) is not None)

o, _ = run('console open\nconsole flush\nconsole put 3 0 "éf"\n'
           'console flush\nconsole close\n')
d = drawn(o)
check("a combining mark rides with its base rather than taking a cell",
      "éf".encode() in d and b"\x1b[4;1H" in d)

o, _ = run('console open\nconsole flush\nconsole pane box 5 10 3 6\n'
           'console put -p box 0 0 "abcdefghij"\n'
           'console put -p box 1 4 "zzzz"\n'
           'console put -p box 9 0 "offpane"\n'
           'console flush\nconsole close\n')
d = drawn(o)
check("a pane clips what is too wide for it", b"\x1b[6;11Habcdef" in d)
check("it clips from the pane's own origin", b"\x1b[7;15Hzz" in d)
check("a row outside the pane draws nothing", b"offpane" not in d)

# `console fill` took no -p until 0.99.174, and its usage line did not
# mention one either -- so every caller that passed one read its positions
# two words late: the row came from the literal "-p" (atoi, 0), the column
# from the pane's name (0), and the glyph from what was meant to be the
# height. Contacts' list divider, `fill -p w2 1 34 16 1 "|"`, was therefore
# one row of 34 columns at the **top left of the screen** filled with the
# text "16" -- a window writing over the menu bar, and staying there,
# since the bar only repaints when its own key changes (Gitea #208).
o, _ = run('console open\nconsole flush\nconsole pane box 5 10 3 6\n'
           'console fill -p box 1 2 1 3 "x"\n'
           'console fill -p nosuch 0 0 1 1 "q" 2> /dev/null\n'
           'echo "ST[$?]"\n'
           'console flush\nconsole close\n')
d = drawn(o)
check("a pane's own fill lands inside it, in the pane's coordinates",
      b"\x1b[7;13Hxxx" in d)
# A rectangle larger than the pane is clipped to it rather than refused:
# the pane is 3 rows of 6 at 5,10, so o's reach rows 6-8 and columns
# 11-16 of the screen and not a cell more.
o2, _ = run('console open\nconsole flush\nconsole pane box 5 10 3 6\n'
            'console fill -p box 0 0 9 99 "o"\n'
            'console flush\nconsole close\n')
d2 = drawn(o2)
check("and a fill wider and taller than the pane is clipped to it",
      b"oooooo" in d2 and b"ooooooo" not in d2
      and b"\x1b[6;11H" in d2 and b"\x1b[9;" not in d2 and b"\x1b[5;" not in d2)
check("nothing a paned fill does reaches the top left of the screen",
      b"\x1b[1;1H" not in d and b"16" not in d)
check("a fill naming a pane that is not there says so and fails",
      b"ST[1]" in o)

o, _ = run('console open\nconsole flush\n'
           'console pane a 5 10 3 20\nconsole pane b 6 15 3 20\n'
           'l := console pane list\necho "L[$l]"\n'
           'console pane raise a\nl := console pane list\necho "L[$l]"\n'
           'console pane lower a\nl := console pane list\necho "L[$l]"\n'
           'g := console pane b\necho "G[$g]"\nconsole close\n')
L = [m.decode() for m in re.findall(rb"L\[([^\]]*)\]", o)]
check("panes list bottom to top in the order they were made",
      L and L[0] == "a b")
check("raise moves a pane to the top", len(L) > 1 and L[1] == "b a")
check("lower moves it back to the bottom", len(L) > 2 and L[2] == "a b")
check("a pane reports its own geometry",
      re.search(rb"G\[6 15 3 20\]", o) is not None)

o, _ = run('console open\nconsole flush\n'
           'console pane a 5 10 3 20\nconsole pane b 6 15 3 20\n'
           'for r in 4 6 6 6 20; do\n'
           '  for c in 5 12 17 40 10; do\n'
           '    h := console hit $r $c\n    echo "H[$r,$c=$h]"\n'
           '  done\ndone\nconsole close\n')
H = dict((m.decode().split("=") + [""])[:2]
         for m in re.findall(rb"H\[([^\]]*)\]", o))
check("a cell outside every pane hits nothing", H.get("4,5") == "")
check("a cell in one pane only hits it", H.get("6,12") == "a")
check("a cell in two hits the topmost", H.get("6,17") == "b")
check("a cell past the right edge hits nothing", H.get("6,40") == "")
check("a cell past the bottom edge hits nothing", H.get("20,10") == "")

o, _ = run('console open\nconsole flush\n'
           'console pane a 5 10 3 20\nconsole pane b 6 15 3 20\n'
           'console pane raise a\nh := console hit 6 17\necho "H[$h]"\n'
           'console pane drop a\nl := console pane list\necho "L[$l]"\n'
           'h := console hit 6 17\necho "H[$h]"\n'
           'if console pane drop a; then echo "TWICE"; fi\nconsole close\n')
check("raising changes what a click finds",
      re.search(rb"H\[a\]", o) is not None)
check("dropping a pane removes it from the stack",
      re.search(rb"L\[b\]", o) is not None)
check("and a click then falls through to what was below",
      o.count(b"H[b]") == 1)
check("dropping a pane that is gone fails", b"TWICE" not in o)

o, _ = run('console open\nconsole flush\n'
           'console pane a 5 10 1 20\nconsole pane b 5 15 1 20\n'
           'console put -p a 0 0 "aaaaaaaaaa"\n'
           'console put -p b 0 0 "bbbbb"\n'
           'console flush\nconsole close\n')
check("the pane drawn last wins the cells they share",
      re.search(rb"\x1b\[6;11Haaaaabbbbb", drawn(o)) is not None)

o, _ = run('console open\nconsole flush\nconsole pen red blue bold\n'
           'console put 1 1 "styled"\nconsole flush\nconsole close\n')
check("the pen becomes one SGR before the run",
      b"\x1b[0;1;38;5;1;48;5;4mstyled" in drawn(o))

o, _ = run('console open\nconsole flush\nconsole pen "#ff8800"\n'
           'console put 1 1 "rgb"\nconsole flush\nconsole close\n')
check("a hex colour becomes a true-colour SGR",
      b"38;2;255;136;0" in drawn(o))

SEQ = [b"\x1b[A", b"\x1b[1;5C", b"\x1b[1;2B", b"\x01", b"\x1b[15~", b"\x1b[H",
       b"\x1b[5~", b"\x1b[Z", b"\x1b[<0;13;4M", b"\x1bx", b"\xc3\xa9", b"\r",
       b"\x1b[200~pasted\x1b[201~", b"\x1b"]
WANT = ["up", "ctrl-right", "shift-down", "ctrl-a", "f5", "home", "pageup",
        "shift-tab", "mouse press left 3 12", "alt-x", "é", "enter",
        "paste pasted", "escape"]
o, _ = run('console open\ni=0\nwhile [ $i -lt %d ]; do\n'
           '  k := console key 2000\n  if [ -z "$k" ]; then break; fi\n'
           '  echo "KEY[$k]"\n  i=$((i+1))\ndone\nconsole close\n' % len(SEQ),
           feed=SEQ, wait=3)
got = [m.decode("utf8", "replace") for m in re.findall(rb"KEY\[([^\]]*)\]", o)]
for i, w in enumerate(WANT):
    check("key %-16s decodes" % repr(w), i < len(got) and got[i] == w)

MOUSE = [b"\x1b[<0;13;4M", b"\x1b[<0;13;4m", b"\x1b[<2;40;10M",
         b"\x1b[<32;41;10M", b"\x1b[<64;5;5M", b"\x1b[<65;5;5M",
         b"\x1b[<66;5;5M", b"\x1b[<67;5;5M",
         b"\x1b[<16;7;7M", b"\x1b[<4;9;9M", b"\x1b\x1b[<0;20;6M"]
# The last is GNU screen's: with mouse reporting on it holds a lone escape
# until the next input, then sends both at once -- an escape, then a click,
# not alt-escape and stray text.
# 66 and 67 are a sideways wheel, or a trackpad's horizontal swipe. Testing
# only bit 0 of the button made them read as wheelup and wheeldown, so a
# sideways scroll moved a list up and down instead.
MWANT = ["mouse press left 3 12", "mouse release left 3 12",
         "mouse press right 9 39", "mouse drag left 9 40",
         "mouse wheelup 4 4", "mouse wheeldown 4 4",
         "mouse wheelleft 4 4", "mouse wheelright 4 4",
         "mouse ctrl-press left 6 6", "mouse shift-press left 8 8",
         "escape", "mouse press left 5 19"]
o, _ = run('console open\nconsole mouse drag\ni=0\nwhile [ $i -lt %d ]; do\n'
           '  k := console key 2000\n  if [ -z "$k" ]; then break; fi\n'
           '  echo "KEY[$k]"\n  i=$((i+1))\ndone\nconsole close\n' % len(MWANT),
           feed=MOUSE, wait=3)
check("mouse reporting is asked for when wanted",
      b"\x1b[?1002h" in o and b"\x1b[?1006h" in o)
check("and turned off again on the way out",
      b"\x1b[?1002l" in o or b"\x1b[?1000l" in o)
mgot = [m.decode("utf8", "replace") for m in re.findall(rb"KEY\[([^\]]*)\]", o)]
for mi, w in enumerate(MWANT):
    check("%-28s decodes" % w, mi < len(mgot) and mgot[mi] == w)

o, _ = run('console open\nconsole flush\nconsole close\n')
check("mouse reporting is off unless asked for",
      b"\x1b[?1002h" not in o and b"\x1b[?1000h" not in o
      and b"\x1b[?1003h" not in o)

expect(r"^usage: console mouse")
o, _ = run('console mouse nonsense\n')
check("a bad mouse mode is refused", b"usage" in o)

# A forked child that is ended by a signal leaves the terminal to the process
# that took it (Gitea #320): the console's fatal-signal cleanup, which every
# fork inherits, restored the terminal's modes and left the alternate screen
# from whichever process was signalled -- a background one was stopped by
# SIGTTOU for touching the terminal (the login screen's watchdog, left as two
# root processes in state T), and any other would have taken the display out
# of the alternate screen under the process still drawing on it.
o, st = run('console open\nconsole flush\nm1=$(stty -g)\n'
            '( sleep 30 ) &\nc=$!\nsleep 0.3\nkill -TERM "$c"\nsleep 0.6\n'
            'cs=$(ps -o stat= -p "$c" 2> /dev/null)\nm2=$(stty -g)\n'
            'console close\necho "child [${cs// /}] modes $([ "$m1" = "$m2" ] && echo same || echo changed)"\n',
            wait=1.0)
tail = o.decode("utf-8", "replace")
check("a forked child ended by a signal is not stopped, and leaves the terminal alone",
      "modes same" in tail and "child [T" not in tail and o.count(b"\x1b[?1049l") == 1,
      (tail[-120:], o.count(b"\x1b[?1049l")))

# A watched pipe nobody reads stops waking the wait (Gitea #323): a desktop
# whose watcher's pipe was no longer drained went round its loop 1,200 times
# a second. Muted after two seconds of that, said so, woken for again once it
# is read; a pipe that is read is never muted.
WLOOP = ('fn spin(secs) {\n local n=0 e\n e=$(( ${EPOCHREALTIME/./} + secs * 1000000 ))\n'
         ' while [ "${EPOCHREALTIME/./}" -lt "$e" ]; do console key 200 > /dev/null; n=$((n + 1)); done\n ret "$n"\n}\n')
expect(r"watched descriptor \d+ has been ready for \d+ ms with nothing reading it")
expect(r"watched descriptor \d+ is being read again")
o, _ = run('console open\nconsole flush\n' + WLOOP +
           'exec {f}< <(while :; do echo x; sleep 0.5; done)\nconsole watch "$f"\n'
           'a := spin 3\nb := spin 1\nw1 := console watching\n'
           'while read -t 0 -u "$f"; do read -r -u "$f" l; done\n'
           'spin 2 > /dev/null\nw2 := console watching\n'
           'console close\necho "W a=$a b=$b w1=[$w1] w2=[$w2] f=$f"\n', wait=8)
m = re.search(rb"W a=(\d+) b=(\d+) w1=\[([^]]*)\] w2=\[([^]]*)\] f=(\d+)", o)
check("a watched pipe nobody reads stops waking the wait after two seconds",
      m and int(m.group(1)) > 50 and int(m.group(2)) <= 8 and m.group(3) == m.group(5) + b":muted",
      m and m.groups() or o[-300:])
check("and it is woken for again once something reads it",
      m and m.group(4) == m.group(5), m and m.groups())
check("and the log names the descriptor", re.search(rb"watched descriptor \d+ has been ready", o) is not None)
o, _ = run('console open\nconsole flush\n'
           'exec {f}< <(while :; do echo x; sleep 0.05; done)\nconsole watch "$f"\nn=0\n'
           'e=$(( ${EPOCHREALTIME/./} + 3000000 ))\n'
           'while [ "${EPOCHREALTIME/./}" -lt "$e" ]; do console key 500 > /dev/null; '
           'while read -t 0 -u "$f"; do read -r -u "$f" l; done; n=$((n + 1)); done\n'
           'w := console watching\nconsole close\necho "W n=$n w=[$w] f=$f"\n', wait=6)
m = re.search(rb"W n=(\d+) w=\[([^]]*)\] f=(\d+)", o)
check("a busy pipe that is read is never muted",
      m and m.group(2) == m.group(3) and int(m.group(1)) > 20, m and m.groups() or o[-300:])

# The kitty keyboard protocol (Gitea #326). A terminal that answers its
# query is asked to tell chords apart (flag 1), once, and given its old
# flags back at close; one that does not answer is never sent the push.
# Every key such a terminal sends is named exactly as the bytes a terminal
# without the protocol would have sent are, and super -- which only this
# protocol can carry -- goes in front: super-c, super on an arrow, and caps
# lock never changing a name. A release and a lone modifier report nothing.
KEYS = [b"\x1b[99;9u", b"\x1b[99;5u", b"\x1b[99;69u", b"\x1b[27u",
        b"\x1b[99;3u", b"\x1b[97;4u", b"\x1b[1;9A", b"\x1b[99;9:3u",
        b"\x1b[57441;9u", b"\x1b[9;2u", b"\x1b[13;3u", b"x"]
KSCRIPT = ('console open\nconsole flush\no=\nfor i in 1 2 3 4 5 6 7 8 9 10; do '
           'k := console key 3000; o+="[$k]"; done\nconsole close\necho "K $o"\n')
def keyrun(caps):
    o, _ = run(KSCRIPT, after=lambda t: [(t.collect(0.4), os.write(t.fd, k),
                                          t.collect(0.08)) for k in KEYS],
               wait=3.0, caps=caps)
    m = re.search(rb"K (\S*)", o)
    return o, (m.group(1).decode() if m else "")
o, keys = keyrun(["kbd"])
check("kitty keyboard keys are named as before, super in front, caps lock "
      "and releases and a lone modifier left out",
      keys == "[super-c][ctrl-c][ctrl-c][escape][alt-c][alt-A][super-up]"
              "[shift-tab][alt-enter][x]", keys)
check("a terminal that answers the keyboard query is asked to tell chords "
      "apart, once, and given its flags back at close",
      o.count(b"\x1b[>1u") == 1 and o.count(b"\x1b[<u") == 1
      and o.index(b"\x1b[>1u") < o.index(b"\x1b[<u"), o[-200:])
o, keys = keyrun([])
check("one that does not answer is never sent the push, and still reads "
      "such keys if they come", b"\x1b[>1u" not in o and b"\x1b[<u" not in o
      and keys.startswith("[super-c][ctrl-c]"), (keys, o[-120:]))
# An escape in the protocol's own form is an escape at once: the 50 ms the
# decoder waits to tell a lone ESC from an alt chord is for the old form.
o, _ = run('console open\nconsole flush\na=${EPOCHREALTIME/./}\n'
           'k := console key 3000\nb=${EPOCHREALTIME/./}\nconsole close\n'
           'echo "E [$k] $(( (b - a) / 1000 ))"\n',
           after=lambda t: (t.collect(0.4), os.write(t.fd, b"\x1b[27u")), wait=2.0)
m = re.search(rb"E \[(\S*)\] (\d+)", o)
check("an escape sent as CSI 27 u is an escape without the alt wait",
      m and m.group(1) == b"escape", m and m.groups())

o, st = run('console open\nconsole flush\nconsole key 5000\n',
            after=lambda t: t.signal(signal.SIGINT), wait=1.5)
check("an interrupt still leaves the alternate screen", b"\x1b[?1049l" in o)
check("an interrupt still gives the cursor back", b"\x1b[?25h" in o)
check("and the signal is not swallowed",
      os.WIFSIGNALED(st) and os.WTERMSIG(st) == signal.SIGINT)


o, _ = run('console open\nconsole flush\nconsole link https://example.com\n'
           'console put 2 2 LINKED\nconsole link -\nconsole put 2 9 plain\n'
           'console flush\nn := console flush\necho "SECOND $n"\n'
           'console close\n')
check("a link is drawn as one, and closed where it ends",
      b"\x1b]8;;https://example.com\x07LINKED\x1b]8;;\x07" in o and
      b"\x1b]8;;https://example.com\x07LINKED plain" not in o)
check("and a flush with nothing new still sends nothing", b"SECOND 0" in o)

o, _ = run('console open\nconsole flush\nconsole bell\n'
           'console notify "done: all green"\nconsole close\n')
# The bell is a bare 0x07, which is also how an OSC string ends, so every
# OSC has to come out of the stream before a bare one means anything --
# both endings of one, since the display's own colour queries end with ST
# (Gitea #215) and a pattern that knew only BEL swallowed the bell itself
# along with everything between.
check("console bell rings, and console notify asks for a notification",
      b"\x07" in re.sub(rb"\x1b\][^\x07\x1b]*(\x07|\x1b\\\\)", b"", o) and
      b"\x1b]9;done: all green\x07" in o)


def bigger(t):
    t.resize(40, 100)


o, _ = run('console open\nconsole flush\nconsole key 3000\nconsole close\n'
           'if console resized; then s := console size; echo "R $s"; fi\n',
           after=bigger, wait=3)
check("a resize interrupts the wait and is reported", counts(o) == [40, 100])

# Writes take the size the last check found rather than asking the kernel
# for every cell, so a resize must still reach them: a write past the old
# edge, straight after the terminal grew, lands.
o, _ = run('console open\nconsole flush\nconsole key 3000\n'
           'console put 35 90 FARCELL\nconsole flush\nconsole close\n',
           after=bigger, wait=3)
check("a write after a resize reaches the new size", b"FARCELL" in o)

# Right-to-left text a script puts is drawn in display order, shaped, for a
# terminal that does no bidi of its own (#68); console bidi off leaves it as
# written, for one that does. Borders and plain text are never touched.
o, _ = run('console open\nconsole put 1 0 "abc سلام 123"\n'
           'console pane win 3 0 4 20\nconsole put -p win 0 0 "مرحبا (ok)"\n'
           'console bidi off\nconsole put 2 0 "x سلام"\n'
           'console bidi on\nconsole put 8 0 "┌─┐ été"\n'
           'console flush\nconsole close\n')
sc = Screen()
sc.feed(o.decode("utf-8", "replace"))
check("right-to-left text is drawn in display order, Arabic in its joined forms",
      sc.row(1).startswith("abc 123 ﻡﻼﺳ"), sc.row(1))
check("in a pane too, clipped before it is ordered, brackets mirrored back",
      sc.row(3).startswith("(ok) ﺎﺒﺣﺮﻣ"), sc.row(3))
check("console bidi off leaves it as written; borders and accents untouched",
      sc.row(2).startswith("x سلام") and
      sc.row(8).startswith("┌─┐ été"), (sc.row(2), sc.row(8)))

# A shadow is cast on every frame, so it has to be idempotent: darkening is
# multiplicative, and `console darken -s` leaves a cell it has already
# shadowed alone. Three releases tried instead to work out which frame was
# allowed to cast one, and got it wrong three times -- shadows pulsing under
# the bar and the menus, the Control Strip blinking, a note's shadow
# flickering while a window moved. Cast once against cast three times must
# be the same bytes; plain darken, three times, must not be.
# In RGB, which is what a theme's colours are: darkening a palette colour
# lands on another palette entry and is idempotent by accident, so a test in
# white-on-blue would have shown nothing wrong with the old behaviour at all.
def shade(cmd):
    o, _ = run('console open\nconsole pen "#c0c0c8" "#203048"\n'
               'console fill 0 0 3 8 " "\n' + cmd +
               'console flush\nconsole close\n')
    sc = Screen()
    sc.feed(o.decode("utf-8", "replace"))
    return sc.p[1][2]


one = shade('console darken -s 1 1 1 4 55\n')
three = shade('console darken -s 1 1 1 4 55\n' * 3)
plain = shade('console darken 1 1 1 4 55\n' * 3)
check("a shadow cast three times is the shadow cast once", one == three,
      (one, three))
check("and without -s it is not, which is what makes that worth saying",
      one != plain and plain != "", (one, plain))
check("a cell written again loses the mark, so its shadow is cast afresh",
      shade('console darken -s 1 1 1 4 55\nconsole pen "#c0c0c8" "#203048"\n'
            'console fill 0 0 3 8 " "\nconsole darken -s 1 1 1 4 55\n')
      == one, one)

# A pane marked `top` is above everything: its cells are written only by
# drawing through it, whatever else is drawn over it and in whatever order.
# That is the desktop's menu bar, which a window dragged over it, a window's
# shadow, a menu or an overlay drew over before -- a window filling the
# display above the primary cast its shadow on the bar. Text written at
# absolute coordinates, through another pane, a fill and a shadow all go
# under it; the bar's own drawing still lands; and `console hit` answers the
# top pane even with a window raised over it.
def top(cmd, row=0):
    o, _ = run('console open\nconsole pane bar 0 2 1 10\n'
               'console pane top bar\nconsole pane w1 0 0 3 20\n'
               'console pane raise w1\n' + cmd +
               'h := console hit 0 4\nconsole put 2 0 "hit=$h"\n'
               'console flush\nconsole close\n')
    sc = Screen()
    sc.feed(o.decode("utf-8", "replace"))
    return sc


sc = top('console put -p bar 0 0 BARBARBAR!\n'
         'console put 0 0 xxxxxxxxxxxxxxxx\n'
         'console put -p w1 0 0 wwwwwwwwwwwwwwww\n'
         'console fill 0 0 1 16 "#"\n')
check("nothing written over a top pane covers it: absolute, another pane, "
      "a fill", sc.row(0).startswith("##BARBARBAR!####"), sc.row(0))
check("and a top pane answers a hit even with a window raised over it",
      sc.row(2).startswith("hit=bar"), sc.row(2))
sc = top('console pen "#c0c0c8" "#203048"\nconsole fill 0 0 2 16 " "\n'
         'console put -p bar 0 0 "          "\n'
         'console darken -s 0 0 1 16 55\nconsole darken 0 0 1 16 55\n')
check("a shadow cast over it darkens the cells beside it and not its own",
      sc.p[0][2] == sc.p[1][2] and sc.p[0][0] != sc.p[1][0],
      (sc.p[0][0], sc.p[0][2], sc.p[1][0]))
sc = top('console put -p bar 0 0 BARBARBAR!\nconsole pane top bar off\n'
         'console put 0 0 xxxxxxxxxxxxxxxx\n')
check("and `pane top name off` gives its cells back",
      sc.row(0).startswith("xxxxxxxxxxxx"), sc.row(0))

report(len(WANT) + len(MWANT) + 66)
