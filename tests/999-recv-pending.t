# recv -p takes what is waiting on a descriptor now and never waits: a part
# of a line, nothing when nothing is, the rest when it comes, and a failing
# status at the end. The desktop reads app processes' sockets this way
# (wm/rtserve.hibr, ADR 0050), where a peer's half line must hold nothing up.

T=/tmp/hibr-recvp-$$
mkdir -p "$T"
listen -b "$T/s" L
( exec {f}<> "/dev/unix$T/s"; printf 'one\ntw' >&$f; sleep 0.6; printf 'o\n' >&$f; sleep 0.4 ) &
sleep 0.3
accept -t 0 "$L" c || echo "no connection"
recv -p "$c" a; echo "waiting: [$a] status $?"
t0=${EPOCHREALTIME/./}
recv -p "$c" b; st=$?
t=$(( ${EPOCHREALTIME/./} - t0 ))
echo "nothing waiting: [$b] status $st, waited: $([ "$t" -lt 100000 ] && echo no || echo "$t us")"
sleep 0.6
recv -p "$c" d; echo "the rest: [$d] status $?"
sleep 0.6
recv -p "$c" e; echo "at the end: status $?"
recv -p 2>&1; echo "no descriptor: status $?"
wait
rm -rf "$T"
