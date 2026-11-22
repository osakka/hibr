# A terminal's program the desktop owns (ADR 0050, route A, mods/pty far.c)
# and the zygote's fork (mods/zygote.c): one process serves -- it starts each
# program as its own child and hands the master over a channel -- and the
# processes it serves are forked from it as the zygote forks app processes.
# Each status here is the one the serving process, the program's parent,
# read; a client that is not the parent would read 0 for all of them.
# Recorded: bash has neither module.
mod load ./build/mods/zygote.so
mod load ./build/mods/pty.so
out=$(mktemp -d)

# What the serving side said, numbers taken out: a verb and what follows the
# program's number and pid; a bare number first is a program hung up.
said() {
	local v n p rest
	while read -r v n p rest; do
		case $v in
		"") ;;
		[0-9]*) echo "hung up: $p" ;;
		*) echo "$v${rest:+ $rest}" ;;
		esac
	done <<< "$1" >> "$out/served"
}
# Serve channel d for owner demo until it ends, at most five seconds.
serve() {
	local l s end=$((SECONDS + 5))
	while [ "$SECONDS" -lt "$end" ]; do
		l := pty serve "$1" demo
		s=$?
		said "$l"
		l := pty reap 300
		said "$l"
		[ "$s" = 1 ] && return 0
		sleep 0.02
	done
}

x := zygote pair
read -r d a <<< "$x"
pid := zygote fork -k "$a"
if [ "$pid" = 0 ]; then
	read -r me _ < /proc/self/stat
	[ "$$" = "$me" ] && echo "the fork has its own \$\$"
	pty via "$a"
	cd "$out"
	export FARMARK=carried
	p := pty spawn -r 10 -c 40 sh -c 'echo "in ${PWD##*/x} with $FARMARK"; sleep 0.3; exit 3'
	o := pty drain "$p" 800
	o=${o//$'\r'/}
	o=${o%%$'\n'*}
	[ "$o" = "in ${out##*/x} with carried" ] && echo "started where the client is, with its environment"
	st := pty wait "$p" 3000
	echo "exit 3: $st"
	q := pty spawn sh -c 'sleep 30 & exit 4'
	st := pty wait "$q" 3000
	echo "exit 4 with its terminal still held open: $st"
	r := pty spawn sleep 30
	pty signal "$r" 15
	st := pty wait "$r" 3000
	echo "ended by TERM: $st"
	l := pty spawn -x sh -c 'exit 6'
	st := pty wait "$l" 3000
	echo "-x is the client's own child: $st"
	k := pty spawn sh -c 'echo $$ > '"$out/kept"'; sleep 30'
	kp := pty pid "$k"
	echo "$kp" > "$out/kp"
	pty close "$p"
	pty close "$q"
	pty close "$r"
	# The channel ends with the program left running: held by nobody.
	pty release "$k"
	exit 0
fi
exec {a}>&-
serve "$d" > /dev/null
l := zygote reaped
echo "the zygote read its fork's status: ${l#* }"
kp=$(cat "$out/kp")
kill -0 "$kp" 2> /dev/null && echo "a program its client let go of runs on, held by nobody"

# A second client takes it back by its pid: its master handed over again.
x := zygote pair
read -r d a <<< "$x"
pid := zygote fork -k "$a"
if [ "$pid" = 0 ]; then
	pty via "$a"
	k := pty adopt -1 "$(cat "$out/kp")" 10 40
	[ -n "$k" ] && pty alive "$k" && echo "a fresh client takes it back by its pid, running"
	pty signal "$k" 15
	st := pty wait "$k" 3000
	echo "and reads the status its parent read: $st"
	pty close "$k"
	exit 0
fi
exec {a}>&-
serve "$d" > /dev/null
l := zygote reaped
echo "the second fork ended: ${l#* }"

# One no client takes back is hung up once its time is up.
x := zygote pair
read -r d a <<< "$x"
pid := zygote fork -k "$a"
if [ "$pid" = 0 ]; then
	pty via "$a"
	k := pty spawn sleep 30
	kp := pty pid "$k"
	echo "$kp" > "$out/kp"
	pty release "$k"
	exit 0
fi
exec {a}>&-
serve "$d" > /dev/null
kp=$(cat "$out/kp")
end=$((SECONDS + 3))
while [ "$SECONDS" -lt "$end" ] && kill -0 "$kp" 2> /dev/null; do
	l := pty reap 300
	said "$l"
	sleep 0.05
done
kill -0 "$kp" 2> /dev/null || echo "one held by nobody for its time is hung up"
l := zygote reaped
cat "$out/served"
rm -rf "$out"
