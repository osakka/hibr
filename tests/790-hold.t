# Sessions that outlive their terminal. Recorded: none of it is bash's.
#
# Everything happens under a TMPDIR of its own, so the test cannot see or
# disturb a session anyone is really using. A terminal attaching is played by
# the pty module, which runs a hibr on a pty of its own and types into it --
# and closing that pty is exactly what a logout does to a client.

mod load ./build/mods/pty.so
mod load ./build/mods/term.so
mod load ./build/mods/hold.so
H=$PWD/build/hibr
M=$PWD/build/mods
export HIBR_RC=/dev/null
export TMPDIR=$(mktemp -d)
L="mod load $M/pty.so; mod load $M/term.so; mod load $M/hold.so"

# What a client printed, without the escapes and the prompt noise. A full
# rendered frame moves row to row with a cursor jump rather than a newline
# -- \e[<row>;1H, always column 1, since hd_render starts every row there
# -- so that exact escape is turned into one before the rest are stripped,
# or a frame's whole 24 rows would arrive as a single unmatchable line. Each
# row is also padded to the full width with blanks (a real repaint, not the
# program's own bytes), trimmed here for the same reason a real terminal's
# trailing blanks are not part of what a line says.
# uniq at the end because a full repaint with no diffing yet (see the
# render slice's own README note) can resend an already-settled line
# across more than one frame; what matters here is that it showed up, not
# how many times the same unchanged frame repeated it.
seen() {
  local o
  o := pty drain "$1" "${2:-1500}"
  printf '%s' "$o" | tr -d '\r' |
    sed 's/\x1b\[[0-9]*;1H/\n/g; s/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' |
    sed 's/[[:space:]]*$//' |
    grep -x 'v=[0-9]*\|.*\[[a-z0-9]*[: ][^]]*\]\|back rc=[0-9]*' |
    sed 's/^.*\(\[[a-z0-9]*[: ]\)/\1/' | uniq
}

# A session's line in hold list, without its pid.
state() {
  hold list | awk -v n="$1" '$1 == n { print $1, $2 }'
}

# What `stty size` printed, without prompt noise. Same row-jump-to-newline
# and trailing-padding fixes as seen(), and for the same reasons.
wsz() {
  local o
  o := pty drain "$1" "${2:-1500}"
  printf '%s' "$o" | tr -d '\r' |
    sed 's/\x1b\[[0-9]*;1H/\n/g; s/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' |
    sed 's/[[:space:]]*$//' |
    grep -x '[0-9]* [0-9]*' | uniq
}

echo "--- a session starts detached and is listed"
hold new -d t1 /bin/sh -c 'sleep 30'
echo "new: $?"
state t1

echo "--- names and duplicates are refused"
hold new -d ../up /bin/sh 2>/dev/null; echo "a name with a slash: $?"
hold new -d t1 /bin/sh 2>/dev/null; echo "a name in use: $?"
hold attach nosuch 2>/dev/null < /dev/null; echo "attach to nothing: $?"

echo "--- attach, detach with ctrl-\\, and the shell is still there"
hold new -d t2 $H
c := pty spawn $H -c "$L; hold attach t2; echo \"back rc=\$?\""
sleep 0.6
pty write $c 'v=5
'
sleep 0.4
pty write $c $'\x1c'
seen $c
pty close $c
state t2

echo "--- the terminal going away detaches it, as a logout does"
c := pty spawn $H -c "$L; hold attach t2; echo \"back rc=\$?\""
sleep 0.6
pty close $c
sleep 0.3
state t2

echo "--- attached again, from a new terminal, everything is as it was"
c := pty spawn $H -c "$L; hold attach t2; echo \"back rc=\$?\""
sleep 0.6
pty write $c 'echo "v=$v"
'
sleep 0.4
pty write $c 'exit 3
'
seen $c
pty close $c
state t2
echo "gone from the list: $(hold list | grep -c '^t2 ')"

echo "--- hold detach, from outside"
hold new -d t3 $H
c := pty spawn $H -c "$L; hold attach t3; echo \"back rc=\$?\""
sleep 0.6
hold detach t3; echo "detach: $?"
seen $c
pty close $c

echo "--- a second attach takes over from the first"
a := pty spawn $H -c "$L; hold attach t3; echo \"back rc=\$?\""
sleep 0.6
b := pty spawn $H -c "$L; hold attach t3; echo \"back rc=\$?\""
sleep 0.6
seen $a
state t3
pty close $b
pty close $a

echo "--- -m joins alongside a client instead of taking over from it"
hold new -d t5 $H
a := pty spawn $H -c "$L; hold attach t5; echo \"back rc=\$?\""
sleep 0.6
b := pty spawn $H -c "$L; hold attach -m t5; echo \"back rc=\$?\""
sleep 0.6
pty write $a 'echo "v=9"
'
sleep 0.4
echo "a sees the real output:"; seen $a
echo "b sees it too, mirrored, though it typed nothing:"; seen $b
pty write $a $'\x1c'
sleep 0.3
echo "a is gone, b is not:"; state t5
pty write $b $'\x1c'
sleep 0.3
echo "now both are:"; state t5
pty close $b
pty close $a

echo "--- viewport offsets: the pty is sized to the union of all attached"
hold new -d t6 $H
a := pty spawn -r 24 -c 80 $H -c "$L; hold attach t6; echo \"back rc=\$?\""
sleep 0.6
echo "a solo plain attach: the pty matches its own size"
pty write $a 'stty size
'
sleep 0.4
# tail -1: several settled frames can repeat the same reading before the
# session moves on (no diffing yet, see render.c's own note); the last one
# in the window is enough to know the size, without depending on how many
# times it happened to be redrawn first.
wsz $a | tail -1
pty write $a $'\x1c'
sleep 0.3

b := pty spawn -r 24 -c 80 $H -c "$L; hold attach -m t6; echo \"back rc=\$?\""
sleep 0.6
c := pty spawn -r 30 -c 100 $H -c "$L; hold attach -m t6 0 80; echo \"back rc=\$?\""
sleep 0.6
echo "a 24x80 client at 0,0 and a 30x100 client at 0,80: union is 30x180"
pty write $b 'stty size
'
sleep 0.4
wsz $b | tail -1
hold kill t6
pty close $b
pty close $c

echo "--- each attached client sees only its own rectangle of the union"
hold new -d t7 $H
a := pty spawn -r 10 -c 40 $H -c "$L; hold attach t7; echo \"back rc=\$?\""
sleep 0.6
b := pty spawn -r 10 -c 40 $H -c "$L; hold attach -m t7 0 40; echo \"back rc=\$?\""
sleep 0.6
# 40 spaces then a marker built from two halves, so the marker itself never
# appears in the typed line's own echo -- only in the program's real output,
# which lands at column 40: inside b's own rectangle, outside a's.
pty write $a $'x=MA; y=RK; printf "%40s%s%s\\n" "" "$x" "$y"\n'
sleep 0.8
oa := pty drain $a 1500
ob := pty drain $b 1500
echo "a (columns 0-39) sees the marker: $(printf '%s' "$oa" | grep -c MARK)"
echo "b (columns 40-79) sees the marker: $(printf '%s' "$ob" | grep -c MARK)"
hold kill t7
pty close $a
pty close $b

echo "--- a program in a session knows which, and can detach itself"
hold new -d t4 /bin/sh -c 'echo "$HIBR_HOLD" > "$TMPDIR/where"; sleep 30'
sleep 0.3
case $(cat "$TMPDIR/where") in */hibr-hold-*/t4) echo "HIBR_HOLD names t4" ;; esac
( unset HIBR_HOLD; hold detach 2>/dev/null; echo "detach outside a session: $?" )

echo "--- kill ends it"
for n in t1 t3 t4 t5; do hold kill $n; echo "kill $n: $?"; done
sleep 0.3
echo "left: $(hold list | wc -l)"
hold kill t1 2>/dev/null; echo "kill again: $?"

rm -rf "$TMPDIR"
