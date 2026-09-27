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

# What a client printed, without the escapes and the prompt noise. A row
# change moves the cursor with a jump to column 1 rather than a newline --
# \e[<row>;1H -- so that exact escape is turned into one before anything
# else, or a whole frame would arrive as a single unmatchable line. Once
# there is a diff and not just a full repaint, a jump to any *other*
# column is real too -- an unchanged gap skipped rather than resent -- and
# has to become a space, not vanish with the rest of the escapes: dropping
# it silently glues whatever comes after straight onto whatever came
# before, with no separator at all ("30 180" arriving as two writes
# joined by a mid-row goto becomes "30180", not "30 180"). Each row is
# also padded to the full width with blanks on a full repaint (a real
# frame, not the program's own bytes), trimmed here for the same reason a
# real terminal's trailing blanks are not part of what a line says.
# uniq at the end because a settled frame can still repeat unchanged
# content the diff already sent once, across more than one poll; what
# matters here is that it showed up, not how many times.
seen() {
  local o
  o := pty drain "$1" "${2:-1500}"
  printf '%s' "$o" | tr -d '\r' |
    sed 's/\x1b\[[0-9]*;1H/\n/g; s/\x1b\[[0-9]*;[0-9]*H/ /g;
         s/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' |
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
    sed 's/\x1b\[[0-9]*;1H/\n/g; s/\x1b\[[0-9]*;[0-9]*H/ /g;
         s/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' |
    sed 's/[[:space:]]*$//' |
    grep -x '[0-9]* [0-9]*' | uniq
}

echo "--- a session starts detached and is listed"
hold new -d t1 /bin/sh -c 'sleep 120'
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

echo "--- a settled frame is a diff: only what changed, resent in full only"
echo "--- on the first attach or a resize, not on every frame after that"
# Thresholds throughout, never the exact byte count: it depends on the
# prompt's own length (this checkout's own path is part of it, and so is
# wherever it happens to be cloned), which varies the digit count of the
# cursor position a frame ends with. The point being proven -- a full
# repaint is hundreds of bytes and a settled diff is not -- holds
# regardless of exactly how many.
hold new -d t12 $H
a := pty spawn -r 24 -c 80 $H -c "$L; hold attach t12; echo \"back rc=\$?\""
sleep 0.6
o1 := pty drain $a 1500
echo "first frame, a full repaint: $(( ${#o1} > 500 ? 1 : 0 ))"
pty write $a 'x'
sleep 0.4
o2 := pty drain $a 1500
echo "one keystroke's own echo is a small diff: $(( ${#o2} < 100 ? 1 : 0 ))"
pty resize $a 30 100
sleep 0.4
pty drain $a 1500 > /dev/null
# The resize alone proves nothing by itself: an idle shell, like real bash,
# does not redraw just because its window changed size, so there is no
# frame at all to inspect yet -- only once it next writes anything does
# whether that write was a diff or a full repaint become observable.
pty write $a 'y'
sleep 0.4
o3 := pty drain $a 1500
echo "a resize invalidates it, full repaint again: $(( ${#o3} > 500 ? 1 : 0 ))"
hold kill t12
pty close $a

echo "--- mode state (alt screen here) is sent on change, not every frame"
hold new -d t9 $H
a := pty spawn $H -c "$L; hold attach t9; echo \"back rc=\$?\""
sleep 0.6
pty write $a $'printf "\\033[?1049h"\n'
sleep 0.6
o1 := pty drain $a 1500
echo "alt screen entered reaches the client: $(printf '%s' "$o1" | cat -v | grep -Fc '^[[?1049h')"
pty write $a 'printf "still here"
'
sleep 0.6
o2 := pty drain $a 1500
echo "not resent once nothing changed: $(printf '%s' "$o2" | cat -v | grep -Fc '^[[?1049h')"
hold kill t9
pty close $a

echo "--- a just-attached client is rendered at once, program or no program"
# cat -v is the vehicle on purpose: unlike a shell, it never reacts to its
# own SIGWINCH, so a client attaching to it gets nothing at all unless hold
# itself sends a first frame rather than waiting on the program to redraw.
hold new -d t10 /bin/sh -c 'stty raw -echo; printf "\033[?1002h\033[?1006h"; cat -v'
a := pty spawn -r 5 -c 100 $H -c "$L; hold attach t10; echo \"back rc=\$?\""
sleep 0.6
b := pty spawn -r 5 -c 60 $H -c "$L; hold attach -m t10 0 40; echo \"back rc=\$?\""
sleep 0.8
ob1 := pty drain $b 1500
echo "b got its mode escapes immediately, unprompted: $(printf '%s' "$ob1" | cat -v | grep -Fc '^[[?1006h')"

echo "--- a client's own mouse report is translated by its offset"
# The held program echoes raw bytes back (cat -v); its own cursor starts at
# column 0, so client a, at offset 0, is what shows what the program itself
# received. Client b, at column offset 40, is the one clicking, at its own
# local column 5 -- the program has to see column 45, not 5.
pty write $b $'\033[<0;5;3M'
sleep 1.0
oa := pty drain $a 2000
echo "b's local column 5 reaches the program as column 45: $(printf '%s' "$oa" | cat -v | grep -Fc '^[[<0;45;3M')"
hold kill t10
pty close $a
pty close $b

echo "--- re-encoded to legacy for a program that never asked for SGR"
# Same shape, but the program only enables 1000 (the "old encoding" case
# 760-term.t also covers) -- b's own terminal is still told to use SGR
# (that is what mtrans can parse), but the program has to receive what it
# actually asked for: legacy X10, \e[M + three raw bytes, button+32,
# column+32, row+32. 'M' is 0x4D = 77 = 45+32; '#' is 0x23 = 35 = 3+32.
hold new -d t11 /bin/sh -c 'stty raw -echo; printf "\033[?1000h"; cat -v'
a := pty spawn -r 5 -c 100 $H -c "$L; hold attach t11; echo \"back rc=\$?\""
sleep 0.6
b := pty spawn -r 5 -c 60 $H -c "$L; hold attach -m t11 0 40; echo \"back rc=\$?\""
sleep 0.8
pty write $b $'\033[<0;5;3M'
sleep 1.0
oa2 := pty drain $a 2000
# Strip hold's own escape sequences first (a real ESC byte, not the literal
# "^[" two characters the held cat -v prints for the mouse report's own
# ESC byte) -- diffing can skip an unchanged cell (here, the report's own
# button byte, a space that already matched an untouched part of the
# screen) and reach the changed ones after it through a goto instead of
# resending everything contiguously, so what actually lands on the client's
# real screen is what matters, not whether "M#" is still directly adjacent
# to the bytes before it in hold's own wire bytes.
echo "re-encoded to legacy, still at column 45: $(printf '%s' "$oa2" |
  sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b>//g' | grep -Fc 'M#')"
hold kill t11
pty close $a
pty close $b

echo "--- an unnamed client gets client-<fd>; a named one keeps its own name"
hold new -d t13 /bin/sh -c 'sleep 120'
a := pty spawn -r 24 -c 80 $H -c "$L; hold attach t13; echo \"back rc=\$?\""
sleep 0.6
echo "default name looks like client-N: $(hold clients t13 | grep -Ec '^client-[0-9]+ ')"
b := pty spawn -r 10 -c 40 $H -c "$L; hold attach -m -n second t13 0 80; echo \"back rc=\$?\""
sleep 0.6
echo "named client keeps its own name: $(hold clients t13 | grep -c '^second ')"
echo "the first to attach is primary: $(hold clients t13 | awk '$1 ~ /^client-/ {print $NF}')"

echo "--- move repositions a named display"
hold move t13 second 0 100
echo "moved: $(hold clients t13 | grep second)"

echo "--- primary transfers to a named display"
hold primary t13 second
echo "primary now: $(hold clients t13 | awk '$1=="second"{print $NF}')"
echo "and the other stops being it: $(hold clients t13 | awk '$1 ~ /^client-/ {print $NF}')"

echo "--- drop detaches a named display and re-picks a primary"
hold drop t13 second
sleep 0.3
echo "gone from clients: $(hold clients t13 | grep -c second)"
echo "the remaining one is primary again: $(hold clients t13 | awk '$1 ~ /^client-/ {print $NF}')"

echo "--- a bad display name is an error, not a crash"
hold move t13 nosuch 0 0 2>/dev/null; echo "move nosuch: $?"
hold drop t13 nosuch 2>/dev/null; echo "drop nosuch: $?"
hold primary t13 nosuch 2>/dev/null; echo "primary nosuch: $?"

hold kill t13
pty close $a
pty close $b

echo "--- a program in a session knows which, and can detach itself"
hold new -d t4 /bin/sh -c 'echo "$HIBR_HOLD" > "$TMPDIR/where"; sleep 120'
sleep 0.3
case $(cat "$TMPDIR/where") in */hibr-hold-*/t4) echo "HIBR_HOLD names t4" ;; esac
( unset HIBR_HOLD; hold detach 2>/dev/null; echo "detach outside a session: $?" )

echo "--- kill ends it"
for n in t1 t3 t4 t5; do hold kill $n; echo "kill $n: $?"; done
sleep 0.3
echo "left: $(hold list | wc -l)"
hold kill t1 2>/dev/null; echo "kill again: $?"

rm -rf "$TMPDIR"
