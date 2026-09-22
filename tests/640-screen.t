# The screen module without a terminal. Drawing itself is tests/screen.py,
# which needs a pty; this is the part run.sh can reach.
mod load ./build/mods/screen.so && echo "loaded"
mod avail 2>/dev/null | grep -c '^screen ' > /dev/null; echo "listed"

# it refuses to open when there is nothing to draw on, rather than hanging
screen open 2>/dev/null; echo "open with no terminal rc=$?"
screen put 0 0 x 2>/dev/null; echo "put while closed rc=$?"
screen flush 2>/dev/null; echo "flush while closed rc=$?"
screen clear 2>/dev/null; echo "clear while closed rc=$?"

# the size is answerable regardless
s := screen size; echo "size fields=$(set -- $s; echo $#)"

# argument checking happens without a terminal too
screen 2>/dev/null; echo "no subcommand rc=$?"
screen nosuchthing 2>/dev/null; echo "bad subcommand rc=$?"
screen pen nosuchcolour 2>/dev/null; echo "bad colour rc=$?"
screen pen red nosuchcolour 2>/dev/null; echo "bad background rc=$?"
screen pen red blue nosuchattr 2>/dev/null; echo "bad attribute rc=$?"
screen pen red blue bold; echo "good pen rc=$?"
screen pen '#ff8800'; echo "hex colour rc=$?"
screen pen 244; echo "palette number rc=$?"
screen pen 999 2>/dev/null; echo "out of range rc=$?"
screen put 2>/dev/null; echo "put with no arguments rc=$?"
screen pane 2>/dev/null; echo "pane with no arguments rc=$?"

# panes are bookkeeping and need no terminal
screen pane a 1 2 3 4; echo "pane defined rc=$?"
screen put -p nosuchpane 0 0 x 2>/dev/null; echo "unknown pane rc=$?"
screen pane clear; echo "panes cleared rc=$?"

mod drop screen && echo "dropped"
