# A request to a session is bounded, so one session in trouble cannot stop
# everything that asks after it (Gitea #204, and #170's symptom with it).
# hd_list dials *every* socket in the directory, and dt_holdnames calls it
# from dt_autohold -- before dt_open, which is what makes desktop.log exist.
# So one listening-and-not-accepting socket was a desktop that printed its
# name and hung for ever, with no error and no log, which is exactly what
# #170 was reported as.
#
# The bound is a default rather than opt-in, where net_conn's own
# HIBR_CONNTIMEO is the other way about: a /dev/tcp script may mean to wait,
# and no caller of hold's control plane does.
#
# Two things the fix has to keep apart, because a timeout is not a refusal.
# hd_serve **unlinks the path before it binds**, so a `hold new` that read a
# timeout as "that name is free" would take a running server's only socket
# away and leave it alive and unreachable; and hd_list removes a socket it
# gets no answer from, which for a merely busy server would be deleting a
# live session. Both are checked here, and both pass against the bug for the
# wrong reason if the stand-in is wrong -- a socket whose listener has gone
# is refused in a millisecond, so the listener has to fill its own queue
# from inside itself and then stay alive (the lesson tests/205-conntimeo.t
# was written around).
#
# Its own TMPDIR, since hd_dir keys the socket directory off it and nothing
# here may reach a session of whoever runs the suite. Recorded: none of it
# is bash's.

command -v python3 > /dev/null 2>&1 || { echo "no python3, skipped"; exit 0; }
d=/tmp/hibr-dial-$$
mkdir -p "$d"
export TMPDIR=$d
export HIBR_MODPATH=$PWD/build/mods
if ! mod load "$PWD/build/mods/hold.so" 2> /dev/null; then
	echo "no hold module, skipped"
	rm -rf "$d"
	exit 0
fi
# hold makes the directory itself, with the 0700 hd_dir insists on.
hold list > /dev/null 2>&1
hd=$d/hibr-hold-$(id -u)
[ -d "$hd" ] && echo "the session directory is ours"

python3 -c '
import socket, sys, time
p = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(p)
s.listen(8)
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
' "$hd/wedged" > "$d/ready" 2> /dev/null &
pid=$!
i=0
while [ ! -s "$d/ready" ] && [ "$i" -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
[ -s "$d/ready" ] || echo "the stand-in never filled its own queue"

echo "--- hold list comes back, with nothing set"
t0=${EPOCHREALTIME/./}
timeout 10 "$SH" -c 'mod load "$HIBR_MODPATH/hold.so" > /dev/null 2>&1; hold list' > "$d/out" 2>/dev/null
echo "status $?"
ms=$(( (${EPOCHREALTIME/./} - t0) / 1000 ))
[ "$ms" -lt 5000 ] && echo "and the default bound is what stopped it waiting"
[ "$ms" -ge 5000 ] && echo "it waited $ms ms, which is the bug"

echo "--- and the session it could not reach is left exactly as it was"
[ -S "$hd/wedged" ] && echo "its socket is still there"
grep -q wedged "$d/out" && echo "it is listed, which it cannot be" ||
	echo "and it is not listed as a session that answered"

echo "--- hold new refuses that name rather than binding over it"
timeout 10 "$SH" -c 'mod load "$HIBR_MODPATH/hold.so" > /dev/null 2>&1
hold new -d wedged /bin/sh -c "sleep 60"' 2>&1
echo "status $?"
[ -S "$hd/wedged" ] && echo "and the socket is still a socket"
n=0
for p in /proc/[0-9]*; do
	read -r c 2> /dev/null < "$p/comm" || continue
	[ "$c" = "hold:wedged" ] && n=$((n + 1))
done
echo "servers started on that name: $n"

echo "--- a socket with no listener at all is still removed as stale"
python3 -c '
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.bind(sys.argv[1])
s.close()
' "$hd/stale"
[ -S "$hd/stale" ] && echo "planted one nobody listens on"
timeout 10 "$SH" -c 'mod load "$HIBR_MODPATH/hold.so" > /dev/null 2>&1; hold list' > /dev/null 2>&1
[ -e "$hd/stale" ] && echo "it is still there, which is the bug"
[ -e "$hd/stale" ] || echo "and that one hold list did remove"

# HIBR_CONNTIMEO is exercised rather than timed: the difference between it
# and the default is smaller than what a release gate's own load does to a
# process start, so a comparison of the two would be a check on the box.
echo "--- HIBR_CONNTIMEO is honoured, and still tells a timeout from a refusal"
HIBR_CONNTIMEO=200 timeout 10 "$SH" -c 'mod load "$HIBR_MODPATH/hold.so" > /dev/null 2>&1; hold list' > /dev/null 2>&1
echo "status $?"
[ -S "$hd/wedged" ] && echo "the wedged session survived a tighter bound too"

kill "$pid" 2> /dev/null
rm -rf "$d"
