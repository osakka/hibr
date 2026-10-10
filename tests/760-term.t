# A terminal emulator: a program's screen, reconstructed as cells.
#
# Recorded, and in shell rather than Python, because the module makes its own
# terminal -- the same reason tests/750-pty.t can be here. Only `term draw`
# needs a display, found lazily on its own first use rather than by term's
# init -- so a server-side user of the "terminal" interface (hold, for its
# own emulator) never drags console in just by loading term. None of this
# file's own commands ever call `term draw`, so no display module is loaded
# here at all; `term draw` itself is covered through a pty in tests/apps.py,
# where a real display is loaded.

mod load ./build/mods/pty.so
mod load ./build/mods/term.so
H=$PWD/build/hibr
export HIBR_RC=/dev/null

# Pump until the terminal stops being given anything to read.
#
# A fixed count of polls is a bet on how few reads the program's output
# arrives in, because `term poll` returns as soon as the pty has something:
# thirty `echo`s coalesce into a handful of reads on an idle machine and into
# about thirty under a gate running twenty sanitizer suites side by side, so
# the scrollback check below read `l 18` where it wanted `l 25` and failed a
# release gate (Gitea #179). `term gen` is the question to ask instead -- how
# many times this terminal has been fed -- and two quiet windows rather than
# one, since a child descheduled under load can take longer than a single
# 100 ms wait to say anything. Bounded, so a program that never stops talking
# ends the loop rather than the suite.
pump() {
  local t=$1 g= p= quiet=0 i=0

  while [ $i -lt 80 ]; do
    term poll "$t" 100
    g := term gen "$t"
    if [ "$g" = "$p" ]; then
      quiet=$((quiet + 1))
      [ $quiet -ge 2 ] && return 0
    else
      quiet=0
    fi
    p=$g
    i=$((i + 1))
  done
  return 0
}

# Run a program, pump until it stops, and show the screen.
show() {
  local t=$1 n=${2:-6}
  pump "$t"
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

echo "--- DECSCUSR sets the cursor's shape, block by default"
# Each stage waits on a line of input before the next escape, rather than on
# a sleep racing the poll loop, so this cannot flake on a loaded machine.
t := term open -r 4 -c 20 /bin/sh -c \
  'printf a; read x; printf "\033[4 q"; read x; printf "\033[5 q"; \
   read x; printf "\033[q"; read x; printf "\033[ q"; read x'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
c := term cursor $t
echo "default: $c"
term write $t '
'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
c := term cursor $t
echo "steady underline: $c"
term write $t '
'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
c := term cursor $t
echo "blinking bar: $c"
term write $t '
'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
c := term cursor $t
echo "a bare CSI q with no space is not DECSCUSR, and changes nothing: $c"
term write $t '
'
i=0; while [ $i -lt 4 ]; do term poll $t 100; i=$((i+1)); done
c := term cursor $t
echo "CSI SP q with no digit still resets to: $c"
term cursor $t underline
c := term cursor $t
echo "set directly: $c"
term cursor $t nonsense 2>/dev/null; echo "bad shape: $?"
term write $t '
'
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
# A program closes its terminal a moment before its status can be
# collected, and after the hangup a poll returns at once -- so this waits
# for the exit, up to five seconds, rather than counting polls.
i=0
while term alive $t && [ $i -lt 50 ]; do
  term poll $t 100
  sleep 0.1
  i=$((i+1))
done
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
# Poll until the inner shell has caught up rather than a fixed number of
# times: under a full gate's load its prompt redraw lands later than
# fourteen hundred milliseconds of polling, and the rows then read
# "echo 42 / in> echo 42 / 42" -- the same sequence one frame earlier,
# which looks like the editor putting things in the wrong order. It had
# never failed before 0.99.112's own gate and passed every run alone,
# which is what a load-dependent *capture* looks like.
# Row 3 holding the next prompt is the settled state -- row 2 holding the
# answer is one frame before it, and waiting for that read "in> echo 42 /
# 42 / <blank>", which is the same off-by-one-frame in the other direction.
i=0
while [ $i -lt 60 ]; do
  term poll $t 100
  r := term row $t 3
  case $r in *in*) break ;; esac
  i=$((i + 1))
done
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
pump $t
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
pump $t
term scroll $t 2
sb := term scroll $t
echo "scrolled: $sb"
term key $t x
sb := term scroll $t
echo "after a key: $sb"
term close $t

echo "--- the store keeps as many lines as it is told"
t := term open -r 3 -c 20 -s 4 /bin/sh -c 'i=1; while [ $i -le 30 ]; do echo "l $i"; i=$((i+1)); done'
pump $t
sb := term scroll $t
echo "stored: $sb"
term scroll $t top
show $t 1
term close $t

echo "--- the alternate screen and ESC [3J stay out of it"
t := term open -r 4 -c 20 /bin/sh -c 'printf "a\nb\nc\nd\ne\n"; printf "\033[?1049h"; i=0; while [ $i -lt 9 ]; do echo alt; i=$((i+1)); done; printf "\033[?1049l"; sleep 1; printf "\033[3J"'
# These two counts are deliberate rather than a bet: the program sleeps a
# second in the middle, so the first reading is of the state before the
# ESC [3J and the second of the state after it. `pump` would run the two
# together.
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

echo "--- what a program sends for the terminal, not the screen"
t := term new -r 4 -c 40
term bell $t; echo "no bell yet: $?"
term feed $t $'ab\a' $'\e]9;build done\a' $'\e]777;notify;make;all green\a' $'\e]9;4;1;50\a'
term bell $t; echo "a bell: $?"
term bell $t; echo "and only once: $?"
n := term note $t; printf 'note [%s]\n' "$n"
n := term note $t; printf 'note [%s]\n' "$n"
term note $t > /dev/null; echo "progress is not a note: $?"
term feed $t $'\e]52;c;aGVsbG8=\a' $'\e]2;Busy\a'
c := term clip $t; echo "clip [$c]"
c := term title $t; echo "title [$c]"
term feed $t $'\e]8;;https://example.com\aLINK\e]8;;\a after'
r := term row $t 0; echo "row [$r]"
term close $t

echo "--- errors"
term open 2>/dev/null; echo "no command: $?"
term poll 999 2>/dev/null; echo "no such terminal: $?"
term nonsense 1 2>/dev/null; echo "bad subcommand: $?"
