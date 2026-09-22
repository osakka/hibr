# The console display without a terminal. Drawing itself is tests/console.py,
# which needs a pty; this is the part run.sh can reach.
mod load ./build/mods/console.so && echo "loaded"
mod avail 2>/dev/null | grep -c '^console ' > /dev/null; echo "listed"

# it refuses to open when there is nothing to draw on, rather than hanging
console open 2>/dev/null; echo "open with no terminal rc=$?"
console put 0 0 x 2>/dev/null; echo "put while closed rc=$?"
console flush 2>/dev/null; echo "flush while closed rc=$?"
console clear 2>/dev/null; echo "clear while closed rc=$?"

# the size is answerable regardless
s := screen size; echo "size fields=$(set -- $s; echo $#)"

# argument checking happens without a terminal too
console 2>/dev/null; echo "no subcommand rc=$?"
console nosuchthing 2>/dev/null; echo "bad subcommand rc=$?"
console pen nosuchcolour 2>/dev/null; echo "bad colour rc=$?"
console pen red nosuchcolour 2>/dev/null; echo "bad background rc=$?"
console pen red blue nosuchattr 2>/dev/null; echo "bad attribute rc=$?"
console pen red blue bold; echo "good pen rc=$?"
console pen '#ff8800'; echo "hex colour rc=$?"
console pen 244; echo "palette number rc=$?"
console pen 999 2>/dev/null; echo "out of range rc=$?"
console put 2>/dev/null; echo "put with no arguments rc=$?"
console pane 2>/dev/null; echo "pane with no arguments rc=$?"

# panes are bookkeeping and need no terminal
console pane a 1 2 3 4; echo "pane defined rc=$?"
console put -p nosuchpane 0 0 x 2>/dev/null; echo "unknown pane rc=$?"
console pane clear; echo "panes cleared rc=$?"

mod drop console && echo "dropped"
