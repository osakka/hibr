# Restart in place: a program on a pty survives the shell exec'ing a new copy
# of itself, which adopts the pty (pty adopt) and the screen and scrollback
# the old one saved (term save, term adopt). Recorded: none of it is bash's.

mod load ./build/mods/pty.so
mod load ./build/mods/term.so
d=${TMPDIR:-/tmp}/hibr-adopt-$$
if [ -z "$ADOPT_STAGE" ]; then
	mkdir -p "$d"
	t := term open -r 6 -c 30 /bin/sh
	term poll $t 300
	term write $t 'X=42; for i in 1 2 3 4 5 6 7 8; do echo line$i; done
'
	n=0
	while [ $n -lt 40 ]; do
		term poll $t 50
		r := term row $t 4
		[ "$r" = line8 ] && break
		n=$((n + 1))
	done
	fd := term fd $t
	pid := term pid $t
	term save $t "$d/t.save" && echo "saved"
	term save $t /nonexistent/dir/x 2> /dev/null || echo "an unwritable save fails"
	ADOPT_STAGE=2 ADOPT_FD=$fd ADOPT_PID=$pid exec "$HIBR" "$0"
fi
pty adopt 99 1 2> /dev/null || echo "a descriptor that is not a terminal is refused"
p := pty adopt $ADOPT_FD $ADOPT_PID 6 30
t := term adopt -r 6 -c 30 $p "$d/t.save"
q := pty pid $p
[ "$q" = "$ADOPT_PID" ] && echo "the same program, adopted"
i=0
while [ $i -lt 6 ]; do r := term row $t $i; echo "screen $i [$r]"; i=$((i + 1)); done
term scroll $t 2
r := term row $t 0
echo "scrollback [$r]"
term scroll $t 0
term write $t 'echo X is $X
'
n=0
while [ $n -lt 40 ]; do
	term poll $t 50
	r := term row $t 4
	[ "$r" = "X is 42" ] && break
	n=$((n + 1))
done
echo "after the exec [$r]"
t2 := term adopt -r 5 -c 30 $p "$d/t.save" 2> /dev/null
r := term row $t2 0
echo "a save of another size loads blank: [$r]"
echo "garbage" > "$d/bad.save"
t3 := term adopt -r 6 -c 30 $p "$d/bad.save" 2> /dev/null
r := term row $t3 0
echo "a damaged save loads blank: [$r]"
term close $t
rm -rf "$d"
