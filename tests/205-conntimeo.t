# A connect is bounded by HIBR_CONNTIMEO, in milliseconds, when it is set
# (Gitea #180). Unset, a connect blocks exactly as it always has, which is
# why the knob is opt-in: `desktop ctl` sets it, and no script that works
# today changes behaviour.
#
# The case that matters is a socket that is **listening with a full accept
# queue and nobody accepting** -- which is what a wedged desktop is, and
# what hung `apps remove` for as long as anybody waited. A *stale* socket
# file is not it: connect then fails at once with ECONNREFUSED, and so does
# a path whose listener has gone, which is why the stand-in here fills its
# own queue from inside itself and then stays alive. Getting that wrong is
# how three earlier versions of this check passed against the bug: the
# listener had exited, the connect was refused in a millisecond, and the
# test proved nothing.
#
# Recorded, and the Unix error text names a mktemp path, so only the status
# is shown for that one.
d=$(mktemp -d)
p=$d/s
python3 -c '
import socket, sys, time
p = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(p)
s.listen(16)
keep = []
while len(keep) < 64:
	c = socket.socket(socket.AF_UNIX)
	c.settimeout(0.3)
	try:
		c.connect(p)
	except OSError:
		break
	keep.append(c)
sys.stdout.write("ready %d\n" % len(keep))
sys.stdout.flush()
while True:
	time.sleep(3600)
' "$p" > "$d/ready" 2> /dev/null &
pid=$!
i=0
while [ ! -s "$d/ready" ] && [ "$i" -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
[ -s "$d/ready" ] || echo "the stand-in never filled its own queue"

echo "--- a connect nobody will accept gives up, rather than waiting"
t0=${EPOCHREALTIME/./}
HIBR_CONNTIMEO=200 timeout 5 "$SH" -c "exec {fd}<>/dev/unix/$p" 2> /dev/null
echo "status $?"
ms=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
[ "$ms" -lt 2000 ] && echo "and it came back without waiting"
[ "$ms" -ge 2000 ] && echo "it waited $ms ms, which is the bug"

echo "--- a refused connect still says refused, not timed out"
HIBR_CONNTIMEO=200 "$SH" -c 'exec {fd}<>/dev/tcp/127.0.0.1/9' 2>&1
echo "status $?"

echo "--- and with nothing set, the same connect is the blocking one it was"
timeout 2 "$SH" -c "exec {fd}<>/dev/unix/$p" 2> /dev/null
echo "status $? (124 is timeout killing it, which is what unbounded means)"

kill "$pid" 2> /dev/null
rm -rf "$d"
