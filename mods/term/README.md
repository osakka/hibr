# `term` — a terminal emulator

The last step of
[decision 0020](../../docs/adr/0020-windows-are-drawn-not-composited.md): a
program runs on a pseudo terminal, and this module keeps a picture of its
screen as cells and paints that picture into a window. What runs inside is a
real program with a real terminal. It edits its own line, draws its own
colours and does not know it is in a window.

It stands on two interfaces and implements neither itself.
[`mods/pty/`](../pty/README.md) opens the pseudo terminal and runs the
program, offered as `mods/pty.h`, and the display (`console`, through
`mods/display.h`) is what `term draw` paints into -- found lazily, on
`draw`'s own first use, not at load time, so a caller that never draws
anywhere never drags console in. It offers `"terminal"` itself, through
`mods/term.h`: a terminal emulator addressed by id, fed bytes and queried by
cell, with nothing drawn anywhere and no pty of its own. `hold` is the first
user of this, for its own emulator over a multi-monitor session's virtual
space; the shell-level builtin below is a second, script-facing user of the
same underlying grid, not the interface itself.

| file | role |
|---|---|
| `tm.h` | the terminal record, cells, and every function the files share |
| `grid.c` | the screen model: cursor, scroll region, deferred wrap, insert and delete of lines and characters, wide characters kept whole, combining sequences, tab stops, resizing |
| `sb.c` | the scrollback: a ring of the lines that went off the top, and the view that scrolls back through it |
| `vt.c` | the escape parser -- Paul Williams' DEC state machine -- and everything it dispatches: cursor movement, erasing, modes, SGR, character sets, save and restore, OSC and DCS strings, and replies to queries |
| `draw.c` | blitting the grid into a rectangle through the display interface |
| `key.c` | turning a decoded key name (`up`, `ctrl-c`, `f5`) back into the bytes a program expects, and a mouse event into the report it asked for |
| `api.c` | the `"terminal"` interface itself: an emulator addressed by id, for a module rather than a script |
| `term.c` | the builtin and the module's life cycle |
| `save.c` | `term save` and the loading half of `term adopt`: a versioned file of named fields, so an older save still loads and an unknown field is skipped |

## The builtin

    t := term open [-r rows] [-c cols] [-s lines] [--] cmd args...
    t := term new [-r rows] [-c cols]  # a terminal with no program: fed by hand
    term poll  t [ms]            # read what the program wrote, parse it
    term draw  t row col h w [curon]  # paint the screen into a rectangle
    term key   t name            # a decoded key name, as the program expects it
    term write t text            # raw bytes, to the program
    term feed  t text... | -f file  # bytes into the emulator itself, as if the program wrote them
    term size  t [rows cols]     # ask, or resize (the program gets SIGWINCH)
    term alive t                 # status 0 while the program runs
    term status t                # its exit status once it has ended
    term title t                 # what it last called itself with OSC 0 or 2
    term clip t                  # text it last put on the clipboard (OSC 52), once
    term bell t                  # status 0 if it rang the bell since last asked
    term note t                  # its next notification (OSC 9, 777): title, tab, text
    term fd    t                 # the pty descriptor, for console watch
    term pid   t                 # the program's process id
    term save  t file            # screen, scrollback, cursor, modes, title -- for a restart
    t := term adopt [-r rows] [-c cols] [-s lines] pty-id [file]
                                 # an emulator for a pty already running (pty adopt),
                                 # loaded from a save of the same size if given
    term cursor t                # row, column, shown, and its shape
    term cursor t block|underline|bar  # set the shape directly
    term row   t n               # one row of what is shown, as text
    term cells t n               # the same row as cells: column, fg, bg, attributes, text
    term focus t 1|0             # tell a program that asked (mode 1004) it gained or lost focus
    term colors t #fg #bg|off    # paint the default colours in these, and answer OSC 10/11 with them
    term scroll t [n|top|bottom] # move the view back n lines, or ask where it is
    term mouse t [act [button] row col]  # send a mouse event, or ask the mode
    term screen t                # main or alt
    term select t start|to r c   # begin a selection at a shown cell, or carry it on
    term select t none
    term copy t                  # the selected text; fails when nothing is
    term close t

Each `term open` is its own terminal, its own session and its own program.
Two windows of `examples/desktop/apps/term.hibr` are two shells on two ptys, and
`tests/apps.py` checks exactly that. The program is given `TERM=xterm-256color`
and `COLORTERM=truecolor` whatever the desktop itself runs in: that is the
terminal this module implements, and a program told it is on tmux or kitty
would send sequences meant for those. It is given a UTF-8 locale for the
same reason, when the one it would inherit is not: in the C locale, screen,
ls and every ncurses program print `?` for what they think cannot be shown
and raw bytes for the rest, which come out as U+FFFD. The C library is
asked what the inherited locale really resolves to -- a UTF-8 name that is
not installed falls back to C without a word -- and only `LC_CTYPE` is set,
from the first of C.UTF-8, UTF-8 (macOS) or en_US.UTF-8 that exists; `LANG`
is replaced only when it names a locale that cannot load, which had left the
program entirely in C anyway, and `LC_ALL` only when it is itself set to a
non-UTF-8 one.

## The parser

`vt.c` is Paul Williams' state machine for DEC terminals, the design most
serious emulators share, fed characters after UTF-8 decoding (an invalid
byte becomes U+FFFD and the byte that broke a sequence is read again). What
follows from it:

- A sequence's prefix (`?`, `>`, `<`, `=`) and intermediates (` `, `$`, `!`)
  are part of what it means. `CSI > 4 ; 2 m` sets a keyboard option, and is
  not SGR underline; `CSI ? u` asks about the kitty keyboard, and is not a
  cursor restore. Reading them without the prefix is what left programs such
  as Claude Code with a stuck underline and text jumping about.
- C0 controls act inside a sequence; CAN and SUB abandon one.
- DCS, OSC, APC, PM and SOS strings are consumed whole, across any number of
  reads, and never printed. OSC 0/1/2 set the title; OSC 10/11 queries are
  answered once `term colors` has given the colours the default text and
  background are then painted in -- the answer is only ever what is on
  screen, and with `off` the question goes unanswered; DECRQSS
  (`DCS $ q`) answers for the scroll region, cursor shape and pen; XTGETTCAP
  (`DCS + q`) says it has nothing, which is still an answer.
- Parameters keep their `:` sub-parameters, so `38:2::r:g:b`, `4:3` (curly
  underline, drawn as plain) and `58;5;n` (underline colour, not drawn) all
  mean what they say and nothing else.

What it implements, by group:

- **Moves:** CUU/CUD stopping at the scroll margins when inside them, CUF,
  CUB, CNL, CPL, CHA, HPA, HPR, VPA, VPR, CUP/HVP honouring origin mode
  (DECOM), IND, NEL, RI, tab stops (HT, HTS, TBC, CHT, CBT).
- **Editing:** ED, EL, ECH (an erase takes the cursor's own cell even with a
  wrap pending, and ends the pending wrap, as DEC STD 070 has it), ICH, DCH,
  IL and DL (which leave the cursor at the line's start), SU, SD, REP, insert
  mode (IRM), newline mode (LNM), DECSTBM, DECALN.
- **Characters:** a combining mark, joiner or variation selector joins the
  cell before it, so `é` written as `e` and U+0301 is one cell; a wide
  character is two cells kept together, and writing over either half blanks
  the other; DEC Special Graphics through `ESC ( 0`, `ESC ) 0`, SO and SI.
- **State:** DECSC/DECRC save and restore the position, pen, character
  sets, origin mode and autowrap mode, one slot per screen; alternate screen
  47, 1047, 1048 and 1049 each with their own mix of clearing and saving;
  DECSTR soft reset and RIS hard reset.
- **Modes:** DECCKM (the arrows and Home/End become `ESC O` sequences),
  DECKPAM/DECKPNM, DECAWM, DECTCEM, focus reports (1004), mouse (9, 1000,
  1002, 1003, 1006), bracketed paste (2004), synchronized output (2026: the
  frame as it was when the program began one is shown until it ends, or for
  a second at most).
- **Replies:** DA1, DA2, DSR 5 and 6 (CPR, relative to the margins under
  DECOM), DECXCPR, DECRQM for every mode above, XTVERSION, the window size
  (`CSI 18 t`).
- **SGR:** bold, dim, italic, underline (and double, as underline), blink,
  reverse, conceal, strike and their resets; 16, 256 and 24-bit colour in
  both spellings.

## Scrollback

What scrolls off the top of the main screen is kept, 1000 lines unless
`term open -s` says otherwise (`-s 0` keeps none). The alternate screen never
adds to it, since what vi and less draw there is not history, and `ESC [3J`
clears it, as `clear` asks. `term scroll t 5` moves the view back five
lines, a negative count forward; `term scroll t` answers `view stored`. A key,
a paste or a mouse event sent to the program goes back to the live screen,
and so does a resize.

A line is stored without its trailing blanks, so the cost is the text: a cell
is 24 bytes, and 1000 lines of 40 characters are about 960 kB. A short session
costs next to nothing.

A window that shrinks under the cursor pushes its top lines into the
scrollback rather than losing the line being typed on, and one that grows
pulls them back, which is xterm's behaviour. Resizing also keeps the
alternate screen, so vi stays on it and redraws.

## Selection

Every line the terminal has held is numbered from the first ever pushed into
the scrollback, and a selection is two of those numbers with a column each.
So it stays on the same text while more output scrolls underneath it, and it
reaches back into the scrollback when the view is scrolled there. It is
drawn reversed; `term copy` gives it as text, each line without its trailing
blanks and joined by newlines, as a terminal's copy does. A resize clears it.

## The mouse

A program asks for the mouse with the private modes every terminal has:
9 (presses), 1000 (presses, releases and the wheel), 1002 (and drags), 1003
(and all motion), and 1006 for the SGR encoding. `term mouse t` answers
`off`, `click`, `drag` or `motion`, and `term mouse t press left 2 4` sends
the report, counted from zero, if the program asked for that kind of event.
It fails if not, so the caller can use the event for something else: the
terminal app scrolls back with the wheel then. Without 1006 the old encoding
is used, which cannot name a column past 223, and a report it cannot encode
is dropped rather than sent wrong.

Bracketed paste (2004) wraps a `paste` key in `ESC [200~` and `ESC [201~`
when the program asks.

## The cursor

A program sets its shape with DECSCUSR (`CSI Ps SP q`): 0, 1 or 2 for a
block, 3 or 4 for an underline, 5 or 6 for a bar; blinking and steady share
one drawn shape, since redrawing on a timer costs a frame in every terminal
window whether or not anyone is looking at it. `term cursor t` reports it as
the fourth field, and `term cursor t <shape>` sets it directly, which is how
a new terminal is given `DT_CURSOR`'s shape before any program has asked for
its own.

`term draw` only shows a cursor when its caller passes `curon` -- the desktop
knows which window has focus, this module does not -- and only on the live
screen (`view` 0), never on the scrollback showing something the program did
not put there. A block or underline is drawn as an attribute on the cell
underneath, alongside a selection; a bar is drawn over the character, since
there is no sub-cell mark in a grid of cells.

## Tests

`tests/term_diff.py` feeds the same bytes to this module and to a private
tmux server and compares the screens cell by cell -- text always, colours
and attributes for the SGR cases. It covers each behaviour above with a
short sequence of its own, and the recorded output of `less`, `nano`, `vi`,
`screen` and `whiptail` (in `tests/term/`, recorded from a generated file by
`--record`). Where xterm and tmux disagree this module follows xterm, and
the case says so and asserts xterm's result instead: erasing with a wrap
pending, REP wrapping, line drawing, CHT, IL/DL's cursor, a split wide
character, an invalid byte. It also feeds sequences split across two reads,
which a whole-file comparison cannot.

`tests/760-term.t` drives the screen model through real programs: text,
cursor addressing, erasing, a scroll region, the alternate screen, deferred
wrap, UTF-8 with a wide character, the title, resizing, exit status, a hibr
inside it editing its own line, the scrollback and its limits, and the exact
bytes a program reads for mouse events and a bracketed paste. `tests/apps.py` covers `term draw` and
keys through a pty, in a window of the desktop. The module is clean under
ASan and UBSan with leak detection on that test.

## What it does not do

Motion with no button held is not reported, even under 1003, because the
desktop turns on only click and drag reporting: all motion is a report per
cell crossed. Modifiers on a mouse event are not passed on. Nothing reflows
on a resize. Left and right margins (DECLRMM), the kitty keyboard protocol,
sixel and the kitty graphics protocol are not implemented -- the parser
consumes their sequences so nothing leaks onto the screen, and a program
asking about them hears nothing back and falls back, as it would on xterm.
Underline styles and colours are parsed and drawn as a plain underline,
since the display has only the one.
