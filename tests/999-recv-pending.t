# recv -p takes what is waiting on a descriptor now and never waits: a part
# of a line, nothing when nothing is, the rest when it comes, and a failing
# status at the end. The desktop reads app processes' sockets this way
# (wm/rtserve.hibr, ADR 0050), where a peer's half line must hold nothing up.
# Each step waits for its condition, bounded, never for a fixed time: the
# writer is slow under the sanitizers and fast without them.

T=/tmp/hibr-recvp-$$
mkdir -p "$T"
listen -b "$T/s" L
mkfifo "$T/go.fifo" "$T/end.fifo"
( exec {f}<> "/dev/unix$T/s"; printf 'one\ntw' >&$f; read -r -t 10 go < "$T/go.fifo"; printf 'o\n' >&$f; read -r -t 10 go < "$T/end.fifo" ) &
accept -t 10 "$L" c || echo "no connection"
a=
for ((i = 0; i < 1000 && ${#a} < 6; i++)); do
	recv -p "$c" x
	a+=$x
	[ ${#a} -lt 6 ] && sleep 0.01
done
echo "waiting: [$a]"
t0=${EPOCHREALTIME/./}
recv -p "$c" b; st=$?
t=$(( ${EPOCHREALTIME/./} - t0 ))
echo "nothing waiting: [$b] status $st, waited: $([ "$t" -lt 100000 ] && echo no || echo "$t us")"
echo go > "$T/go.fifo"
d=
for ((i = 0; i < 1000 && ${#d} < 2; i++)); do
	recv -p "$c" x
	d+=$x
	[ ${#d} -lt 2 ] && sleep 0.01
done
echo "the rest: [$d]"
echo end > "$T/end.fifo"
st=0
for ((i = 0; i < 1000; i++)); do
	recv -p "$c" x || { st=$?; break; }
	sleep 0.01
done
echo "at the end: status $st"
recv -p 2>&1; echo "no descriptor: status $?"
wait
rm -rf "$T"
