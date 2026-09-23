# Sessions that outlive their terminal. Recorded: none of it is bash's.
#
# Everything happens under a TMPDIR of its own, so the test cannot see or
# disturb a session anyone is really using. A terminal attaching is played by
# the pty module, which runs a hibr on a pty of its own and types into it --
# and closing that pty is exactly what a logout does to a client.

mod load ./build/mods/pty.so
mod load ./build/mods/hold.so
H=$PWD/build/hibr
M=$PWD/build/mods
export HIBR_RC=/dev/null
export TMPDIR=$(mktemp -d)
L="mod load $M/pty.so; mod load $M/hold.so"

# What a client printed, without the escapes and the prompt noise.
seen() {
  local o
  o := pty drain "$1" "${2:-1500}"
  printf '%s' "$o" | tr -d '\r' | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' |
    grep -x 'v=[0-9]*\|.*\[[a-z0-9]*[: ][^]]*\]\|back rc=[0-9]*' |
    sed 's/^.*\(\[[a-z0-9]*[: ]\)/\1/'
}

# A session's line in hold list, without its pid.
state() {
  hold list | awk -v n="$1" '$1 == n { print $1, $2 }'
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

echo "--- a program in a session knows which, and can detach itself"
hold new -d t4 /bin/sh -c 'echo "$HIBR_HOLD" > "$TMPDIR/where"; sleep 30'
sleep 0.3
case $(cat "$TMPDIR/where") in */hibr-hold-*/t4) echo "HIBR_HOLD names t4" ;; esac
( unset HIBR_HOLD; hold detach 2>/dev/null; echo "detach outside a session: $?" )

echo "--- kill ends it"
for n in t1 t3 t4; do hold kill $n; echo "kill $n: $?"; done
sleep 0.3
echo "left: $(hold list | wc -l)"
hold kill t1 2>/dev/null; echo "kill again: $?"

rm -rf "$TMPDIR"
