# A terminal emulator: a program's screen, reconstructed as cells.
#
# Recorded, and in shell rather than Python, because the module makes its own
# terminal -- the same reason tests/750-pty.t can be here. Only `term draw`
# needs a display, and that is covered through a pty in tests/apps.py.

mod load ./build/mods/pty.so
mod load ./build/mods/term.so
H=$PWD/build/hibr
export HIBR_RC=/dev/null

# Run a program, pump until it stops, and show the screen.
show() {
  local t=$1 n=${2:-6} i=0
  while [ $i -lt 14 ]; do term poll "$t" 100; i=$((i + 1)); done
  i=0
  while [ $i -lt "$n" ]; do
    r := term row "$t" $i
    printf '%d|%s\n' $i "$r"
    i=$((i + 1))
  done
}

echo "--- text, newlines and a carriage return"
t := term open -r 6 -c 30 /bin/sh -c 'printf "one\ntwo\nthree\n"; printf "over\rXX\n"'
show $t
term close $t

echo "--- the cursor goes where it is told"
t := term open -r 6 -c 30 /bin/sh -c 'printf "\033[3;10Hplaced\033[1;1Htop"'
show $t 4
c := term cursor $t
echo "cursor $c"
term close $t

echo "--- erasing, inserting and deleting"
t := term open -r 6 -c 30 /bin/sh -c 'printf "abcdefgh\033[1;4H\033[2P|\n"; printf "keepme\033[2;4H\033[K\n"'
show $t 3
term close $t

echo "--- a scroll region scrolls only itself"
t := term open -r 6 -c 20 /bin/sh -c 'printf "1\n2\n3\n4\n5"; printf "\033[2;4r\033[4;1H\n\nX"'
show $t 6
term close $t

echo "--- the alternate screen gives the first one back"
t := term open -r 4 -c 20 /bin/sh -c 'printf "before\n"; printf "\033[?1049h"; printf "\033[HINSIDE"; printf "\033[?1049l"'
show $t 3
term close $t

echo "--- wrapping is deferred to the next character"
t := term open -r 4 -c 8 /bin/sh -c 'printf "12345678"; printf "\rX"'
show $t 3
term close $t

echo "--- utf-8, including a wide character"
t := term open -r 4 -c 20 /bin/sh -c 'printf "héllo 漢字 ok\n"'
show $t 2
term close $t

echo "--- the title comes from the escape that sets it"
t := term open -r 4 -c 20 /bin/sh -c 'printf "\033]0;my title\007hi\n"'
show $t 1
n := term title $t
echo "title [$n]"
term close $t

echo "--- resizing keeps what fits"
t := term open -r 6 -c 30 /bin/sh -c 'printf "keep this line\n"; sleep 0.4; stty size'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
term size $t 10 40
sz := term size $t
echo "size $sz"
show $t 3
term close $t

echo "--- a program that ends says so"
t := term open -r 4 -c 20 /bin/sh -c 'exit 3'
i=0; while [ $i -lt 8 ]; do term poll $t 100; i=$((i+1)); done
if term alive $t; then echo "still running"; else echo "ended"; fi
st := term status $t
echo "status $st"
term close $t

echo "--- a hibr inside, editing its own line"
# Started in /, because its first prompt shows the directory, and a long
# checkout path would wrap it and move every row below.
cd /
t := term open -r 8 -c 40 $H
cd "$OLDPWD"
i=0; while [ $i -lt 8 ]; do term poll $t 100; i=$((i+1)); done
term write $t 'PS1="in> "
'
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
term key $t e; term key $t c; term key $t h; term key $t o
term key $t space; term key $t 4; term key $t 2; term key $t enter
i=0; while [ $i -lt 14 ]; do term poll $t 100; i=$((i + 1)); done
# from row 1: row 0 still holds the prompt this machine happens to have,
# and a recorded test must not depend on whose machine it ran on.
i=1
while [ $i -lt 4 ]; do
  r := term row $t $i
  printf '%d|%s\n' $i "$r"
  i=$((i + 1))
done
term close $t

echo "--- lines that scroll off the top are kept"
t := term open -r 6 -c 20 /bin/sh -c 'i=1; while [ $i -le 20 ]; do echo "line $i"; i=$((i+1)); done'
i=0; while [ $i -lt 8 ]; do term poll $t 100; i=$((i+1)); done
sb := term scroll $t
echo "view and stored: $sb"
term scroll $t 3
show $t 2
term scroll $t top
show $t 2
term scroll $t 100
sb := term scroll $t
echo "past the top stops at the top: $sb"
term scroll $t bottom
show $t 1
term close $t

echo "--- a key goes back to the live screen"
t := term open -r 4 -c 20 /bin/sh -c 'i=1; while [ $i -le 9 ]; do echo "n $i"; i=$((i+1)); done; cat > /dev/null'
i=0; while [ $i -lt 8 ]; do term poll $t 100; i=$((i+1)); done
term scroll $t 2
sb := term scroll $t
echo "scrolled: $sb"
term key $t x
sb := term scroll $t
echo "after a key: $sb"
term close $t

echo "--- the store keeps as many lines as it is told"
t := term open -r 3 -c 20 -s 4 /bin/sh -c 'i=1; while [ $i -le 30 ]; do echo "l $i"; i=$((i+1)); done'
i=0; while [ $i -lt 8 ]; do term poll $t 100; i=$((i+1)); done
sb := term scroll $t
echo "stored: $sb"
term scroll $t top
show $t 1
term close $t

echo "--- the alternate screen and ESC [3J stay out of it"
t := term open -r 4 -c 20 /bin/sh -c 'printf "a\nb\nc\nd\ne\n"; printf "\033[?1049h"; i=0; while [ $i -lt 9 ]; do echo alt; i=$((i+1)); done; printf "\033[?1049l"; sleep 1; printf "\033[3J"'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
sb := term scroll $t
echo "after the alternate screen: $sb"
i=0; while [ $i -lt 14 ]; do term poll $t 100; i=$((i+1)); done
sb := term scroll $t
echo "after ESC [3J: $sb"
term close $t

echo "--- a deleted line is not history"
t := term open -r 4 -c 20 /bin/sh -c 'printf "a\nb\nc\033[H\033[M"'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
sb := term scroll $t
echo "stored: $sb"
show $t 2
term close $t

echo "--- shrinking keeps the cursor's line, growing brings lines back"
t := term open -r 6 -c 20 /bin/sh -c 'printf "1\n2\n3\n4\n5\nhere"; sleep 5'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
term size $t 3 20
sb := term scroll $t
echo "shrunk, stored: $sb"
show $t 3
term size $t 6 20
sb := term scroll $t
echo "grown, stored: $sb"
show $t 6
term close $t

echo "--- the mouse reaches a program that asks for it"
t := term open -r 4 -c 70 /bin/sh -c 'stty raw -echo; printf "ready\r\n"; head -c 1 > /dev/null; printf "\033[?1002h\033[?1006h"; head -c 38 | cat -v'
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
m := term mouse $t
echo "before it asks: $m"
term mouse $t press left 0 0 && echo "sent anyway" || echo "not sent"
term write $t g
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
m := term mouse $t
echo "after it asks: $m"
term mouse $t press left 2 4
term mouse $t drag left 3 5
term mouse $t release left 3 5
term mouse $t wheelup 0 0
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
show $t 2
term close $t

echo "--- in the old encoding, and only what was asked for"
t := term open -r 4 -c 40 /bin/sh -c 'stty raw -echo; printf "\033[?1000h"; head -c 12 | cat -v'
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
term mouse $t drag left 1 1 && echo "drag sent" || echo "no drag in click mode"
term mouse $t press left 0 0
term mouse $t release left 0 0
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
show $t 1
term close $t

echo "--- a paste is bracketed when the program asks"
t := term open -r 4 -c 40 /bin/sh -c 'stty raw -echo; printf "\033[?2004h"; head -c 14 | cat -v'
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
term key $t "paste hi"
i=0; while [ $i -lt 6 ]; do term poll $t 100; i=$((i+1)); done
show $t 1
term close $t

echo "--- errors"
term open 2>/dev/null; echo "no command: $?"
term poll 999 2>/dev/null; echo "no such terminal: $?"
term nonsense 1 2>/dev/null; echo "bad subcommand: $?"
