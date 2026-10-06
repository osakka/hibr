# Full-console programs

The line editor owns one line. The `console` module owns the terminal: an
alternate screen, a grid of cells, a redraw that sends only what changed, and
keys decoded once into names you can compare against.

It is a module, so nothing pays for it until it is loaded. The module is called
`console` because it is a *text* display; what it offers other modules is the
**display** interface in `mods/display.h`, which a framebuffer or SDL backend
could offer just as well. A tool written against that interface would not have
to change.

<!-- not run: it loads the module and prints nothing -->
```sh
mod load console
```

`need console` does the same, and is what a script that only needs *a*
display should say.

## The idea worth understanding first

There are two grids. `console put` writes into the **back** buffer, which is
memory; nothing reaches the terminal until `console flush` compares back against
**front** and emits the difference.

That inverts how drawing usually feels. You do not work out what changed — you
redraw the whole screen, every frame, and let the flush work it out. On an
eighty by twenty-four terminal:

| | bytes actually written |
|---|---|
| the first flush, painting everything | 2095 |
| writing four characters | 11 |
| changing one of them | 8 |
| flushing again with nothing changed | **0** |

The last row is the one that matters. A monitor that samples every second and
redraws its entire layout sends nothing at all when nothing moved.

## A whole program

<!-- not run: needs a terminal: it draws on the console -->
```sh
mod load console
console open || exit 1

while true; do
  sz := console size
  set -- $sz

  console pen black cyan bold
  console fill 0 0 1 "$2" " "
  console put 0 1 "hibr — q to quit"

  console pen default
  console put 2 2 "epoch $EPOCHSECONDS"

  console flush
  k := console key 1000
  case "$k" in q) break ;; esac
done

console close
```

`examples/console-demo.hibr` is the longer version, with panes, a status line
and the flush cost shown live. Run it and press things.

## Commands

| | |
|---|---|
| `console open` | take the terminal: alternate screen, raw mode, no cursor, and a descriptor of its own for it -- so a redirection on a later `console` command cannot reach the display |
| `console close` | give it back exactly as it was found |
| `console size` | rows and columns, as two words |
| `console gfx` | what this terminal does with a picture, and the pixel size of a cell: `kitty 10 20`, `sixel 8 16`, or `none 0 0`. `HIBR_GFX=kitty`, `sixel` or `off` says outright |
| `console resized` | true once after the terminal changed size |
| `console clear` | blank the back buffer with the current pen |
| `console pen [fg [bg [attr…]]]` | the colours and attributes later writes use |
| `console put row col text` | write into the back buffer |
| `console put -p pane row col text` | the same, inside a pane and clipped to it |
| `console fill row col h w [char]` | repeat a character over a rectangle |
| `console cursor row col` / `console cursor off` | where the cursor should be seen |
| `console flush` | send what changed; gives the byte count |
| `console shot [-f ansi\|html\|text] file [row col h w]` | write what is on screen -- the last flush -- or a rectangle of it: `ansi` the cells with their colours as a terminal draws them (`cat` it), `html` the same as a page, `text` the characters alone |
| `console key [ms]` | wait for a key, up to ms; gives its name |
| `console waiting` | true when more input is already there; never waits |
| `console resizing` | true while a resize is pending, without consuming it |
| `console reassert` | send the terminal's modes again and repaint everything on the next flush |
| `console mouse click\|drag\|motion\|off` | which mouse reports to ask for |
| `console signals on\|off` | whether ctrl-c, ctrl-backslash and ctrl-z raise signals or arrive as keys |
| `console darken row col h w [pct]` | shade what is already in the back buffer, a shadow |
| `console darkdefault fg bg` | the colours `darken` takes a cell drawn in the terminal's own default colours to be, so a shadow over it still darkens |
| `console shade colour [pct]` | the colour `darken` would make of a `#rrggbb` |
| `console clip text` | put text on the terminal's clipboard (OSC 52) |
| `console link url\|-` | make later writes a link (OSC 8), or stop |
| `console bell` | ring the terminal's bell |
| `console notify text` | ask the terminal for a notification (OSC 9) |
| `console watch fd` / `console unwatch fd` | let `console key` also wake when a descriptor is readable |
| `console consumed` | how many bytes of input have become keys |
| `console pane name row col h w` | define a region, or move one |
| `console pane name` | its row, column, height and width |
| `console pane raise\|lower\|drop name` | restack a pane, or forget it |
| `console pane list` | every pane, bottom to top |
| `console pane clear` | forget every pane |
| `console hit row col` | the topmost pane at a cell, or nothing |

Rows and columns count from zero. `console flush`, `console key`, `console
size`, `console shade` and `console hit` fill the result slot, so `n := console
flush` and `k := console key 1000` are how you read them without forking.
`console shade`, and the pane commands with `console hit` (see
[Panes](#panes)), work before `console open`, with no terminal at all:

```sh
need console
c := console shade "#ffffff"
echo "white, shaded: $c"
console shade "#ff8800" 50
```

```output
white, shaded: #8c8c8c
#804400
```

## Colour

A colour is a name, a palette number from 0 to 255, or `#rrggbb`:

<!-- not run: needs a terminal: the pen is used by what is drawn after it -->
```sh
console pen red                 # foreground only
console pen white blue          # foreground and background
console pen yellow default bold # and attributes
console pen 244                 # the 256-colour palette
console pen '#ff8800'           # true colour
console pen                     # back to the terminal's own
```

Names are `black red green yellow blue magenta cyan white` and the `bright`
forms — `brightred`, `brightwhite`. Attributes are `bold dim italic underline
blink reverse strike`, and any number of them may follow the two colours.

The pen applies to everything written after it, and the flush emits one SGR
sequence before each run that needs a different one, not one per character.

## Keys

`console key` returns a name, never a byte:

```text
up  down  left  right  home  end  pageup  pagedown  insert  delete
f1 … f12  tab  shift-tab  enter  backspace  escape
ctrl-a … ctrl-z  ctrl-space
```

Modifiers prefix the name in a fixed order — `ctrl-`, then `alt-`, then
`shift-` — so `ctrl-right`, `shift-down`, `ctrl-alt-f5`. A printable character
comes back as itself, including multi-byte ones: `é` is one key, and the
space bar is `" "`, not `space`.

Two arrive with more than a name:

```text
mouse press left 3 12    the action, the button, then row and column
paste some text here     everything between the paste markers
```

A lone escape is reported as `escape`, but only after 50 ms with nothing
following it — otherwise there would be no way to tell it from the start of an
arrow key. Over a link slow enough to split a sequence across that gap, an
arrow key can arrive as `escape` and then its letters; this is the same trade
every terminal program makes.

`console key 1000` waits up to a second and gives nothing back if the second
passes. `console key` with no argument waits forever. Either way a terminal
resize ends the wait early, so a loop that draws on a timer also redraws
promptly when the window changes:

<!-- not run: needs a terminal: it waits for a key -->
```sh
k := console key 1000
if console resized; then relayout; console clear; fi
```

## Keeping up with a burst of input

A frame that takes longer to draw than the gap between two reports -- a drag
across a large terminal, a held wheel -- leaves the reports queued, and a
program that draws once per report goes on replaying them after the hand has
stopped. `console waiting` answers, without waiting, whether the next report
is already there; skip the frame while it is, and draw once it is not:

<!-- not run: needs a terminal: it reads keys and draws -->
```sh
while true; do
  k := console key 1000
  handle "$k"
  console waiting && continue   # more already queued: this frame is stale
  draw
  console flush
done
```

The desktop does exactly this for drags and the wheel: a burst of a hundred
drag reports draws two frames, not a hundred.

## Keys that would be signals

By default ctrl-c interrupts a full-screen program the way it interrupts
anything else, and the console puts the terminal back on the way out. A
program that should be left only through its own quit can have those keys
instead:

<!-- not run: needs a terminal: it changes how the terminal treats keys -->
```sh
console signals off     # ctrl-c, ctrl-\ and ctrl-z arrive as keys
console signals on
```

## The mouse

Nothing is reported until it is asked for, because reporting takes the
terminal's own text selection away from the person watching:

<!-- not run: needs a terminal: it asks the terminal for mouse reports -->
```sh
console mouse click     # presses and releases
console mouse drag      # and dragging
console mouse motion    # and every movement
console mouse off
```

Reports arrive through `console key` like any other key:

```text
mouse press left 3 12        the action, the button, then row and column
mouse release left 3 12
mouse drag left 9 40
mouse wheelup 4 4            the wheel has no button
mouse ctrl-press left 6 6    modifiers prefix the action
```

Row and column count from zero, the same as `put`, so a click can be used as a
position directly:

<!-- not run: needs a terminal: it waits for a click -->
```sh
k := console key
case "$k" in
  "mouse press"*)
    set -- $k
    console put "$4" "$5" "X"
    ;;
esac
```

## Panes

A pane is a named rectangle. Writing through one uses the pane's own
coordinates and is clipped to its edges, so a program does not have to check
whether text fits:

<!-- not run: needs a terminal: it draws on the console -->
```sh
console pane body 2 2 20 40
console put -p body 0 0 "this is clipped at forty columns, however long it is"
console put -p body 99 0 "this row is outside the pane, so nothing is drawn"
```

Panes are bookkeeping, not windows — they hold no content of their own. They
are stacked, though: `console pane raise`, `lower` and `drop` reorder them,
`console pane list` gives them bottom to top, and `console hit row col` names
the topmost one at a cell, which is how the desktop knows what was clicked.
Layout policy belongs to the program; the pane just saves it the arithmetic.

```sh
need console
console pane body 2 2 20 40
console pane side 0 0 5 10
console pane list
console hit 3 5
console pane raise body
console pane list
console hit 3 5
console pane body
```

```output
body side
side
side body
body
2 2 20 40
```

## Wide and combining characters

The module uses the same width tables as the line editor. A wide glyph takes
two cells, and overwriting either half clears both, so half a character is
never left behind:

<!-- not run: needs a terminal: what matters is the bytes it sends -->
```sh
console put 1 0 "漢字ab"   # 漢 occupies columns 0 and 1
console put 1 1 "X"        # writing over its right half
```

sends `\e[2;1H X` — a space where the orphaned left half was, then the `X`.

A combining mark takes no cell of its own and stays with the character it
belongs to, so `e` followed by U+0301 is one cell holding `é`, and the text
after it is not shifted.

## Getting the terminal back

This is the part that matters more than the drawing. `SIGINT`, `SIGTERM` and
`SIGHUP` restore the terminal — alternate console off, cursor back, raw mode
undone — and then re-raise the signal, so the shell still dies of whatever
killed it. `mod drop console` and shell exit do the same.

`console open` with no terminal to draw on fails with status 1 and a message; it
does not hang and does not half-open. So a script that might be run from cron
can simply check:

```sh
need console
console open || { echo "needs a terminal" >&2; exit 1; }
```

```output
hibr: console: no terminal to draw on
needs a terminal
```

## What it does not do

There is no terminfo and no `TERM` lookup — see
[0019](adr/0019-the-console-display-assumes-xterm.md) for why, and what that
costs. There is no scrolling region, no line-drawing character set beyond
whatever Unicode you write yourself, and no input line editing: that is the
line editor's job, and a full-console program that wants a prompt should draw
one itself.

---

[← documentation index](README.md)
