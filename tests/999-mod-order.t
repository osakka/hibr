# A module whose init loads another goes after it in the module list, which
# is dropped from the end at exit: term loads pty for itself, and with term
# first a script that left a terminal open had pty unmapped before term's
# finaliser called it -- SIGSEGV at every such exit, and the script's last
# buffered output lost with it (2026-11-17). The same with nothing left open
# is the control.

HIBR_MODPATH=./build/mods "$HIBR" -c 'mod load ./build/mods/term.so; t := term open -r 5 -c 20 sleep 1; echo opened'
echo "left open: status $?"
HIBR_MODPATH=./build/mods "$HIBR" -c 'mod load ./build/mods/term.so; t := term open -r 5 -c 20 sleep 1; term close "$t"; echo closed'
echo "closed: status $?"
