# Pseudo terminals. Recorded, because none of it is bash behaviour.
#
# Note what this file is: a *shell* test of terminal handling. Every other
# full-screen suite is Python, because the harness has to be the controlling
# process of a pty and hibr could not open one. It can now, so this is run by
# tests/run.sh like anything else.

mod load ./build/mods/pty.so
H=$PWD/build/hibr

# The inner shells must not read whatever .hibrc this machine happens to
# have, or the recorded output is one person's prompt.
export HIBR_RC=/dev/null

echo "--- a program gets a terminal of its own"
i := pty spawn /bin/sh -c 'test -t 1 && echo "stdout is a tty"; tty'
o := pty drain $i 900
printf '%s' "$o" | tr -d '\r' | sed 's|/dev/pts/[0-9]*|/dev/pts/N|'
st := pty wait $i 900
echo "status $st"
pty close $i

echo "--- the size is what was asked for, and resize is seen"
i := pty spawn -r 12 -c 34 /bin/sh -c 'stty size'
o := pty read $i 900
printf '%s' "$o" | tr -d '\r'
pty close $i

i := pty spawn -r 9 -c 20 /bin/sh -c 'sleep 0.3; stty size'
pty resize $i 40 100
sz := pty size $i
echo "size says $sz"
o := pty drain $i 1200
printf '%s' "$o" | tr -d '\r'
pty close $i

echo "--- what is written arrives"
i := pty spawn /bin/sh
pty write $i 'echo "typed in"
'
o := pty read $i 700
printf '%s' "$o" | tr -d '\r' | grep -c 'typed in' | sed 's/^/saw it echoed and run: /' 
pty write $i 'exit 5
'
st := pty wait $i 1200
echo "status $st"
pty close $i

echo "--- a status, a signal and a missing command"
i := pty spawn /bin/sh -c 'exit 7'
st := pty wait $i 900
echo "exit 7 gives $st"
pty close $i

i := pty spawn /bin/sh -c 'sleep 30'
pty signal $i 9
st := pty wait $i 900
echo "killed gives $st"
pty close $i

i := pty spawn /nonexistent-command-xyz
st := pty wait $i 900
echo "missing gives $st"
pty close $i

echo "--- alive, list and close"
i := pty spawn /bin/sh -c 'sleep 30'
if pty alive $i; then echo "alive while running"; fi
j := pty spawn /bin/sh -c 'sleep 30'
l := pty list
echo "two open: $(echo $l | wc -w)"
pty close $i
pty close $j
l := pty list
echo "none left: [${l}]"

echo "--- reading past the end fails rather than blocking"
i := pty spawn /bin/sh -c 'echo last'
o := pty read $i 900
printf '%s' "$o" | tr -d '\r'
pty wait $i 900 > /dev/null
if pty read $i 200 > /dev/null; then echo "still open"; else echo "read past the end fails"; fi
pty close $i

echo "--- an interactive hibr, on a terminal hibr made"
i := pty spawn -r 24 -c 60 $H -c 'echo $((6 * 7)) from inside'
o := pty drain $i 1200
st := pty wait $i 1200
pty close $i
printf '%s' "$o" | tr -d '\r' | grep -c '42 from inside' | sed 's/^/saw the answer: /'
echo "inner exit $st"

echo "--- and an interactive one, with its line editor running"
i := pty spawn -r 24 -c 60 $H
pty write $i 'PS1="inner> "
'
o := pty read $i 600
pty write $i 'exit
'
st := pty wait $i 1500
pty close $i
printf '%s' "$o" | tr -d '\r' | grep -c 'inner> ' | sed 's/^/drew its own prompt: /'
echo "inner exit $st"

echo "--- errors"
pty spawn 2>/dev/null; echo "no command: $?"
pty read 999 2>/dev/null; echo "no such pty: $?"
pty nonsense 2>/dev/null; echo "bad subcommand: $?"
