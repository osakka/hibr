# HIBR_CONNTIMEO bounds a connect so a desktop that has stopped accepting
# cannot hang its caller, and it has to mean the same on /dev/unix as on
# /dev/tcp (Gitea #466). A non-blocking AF_UNIX connect against a full
# backlog does not wait: it answers EAGAIN at once, so the bounded connect
# failed in a millisecond, and `desktop ctl` called a desktop whose queue was
# momentarily full a desktop that had stopped. The connect itself is retried
# until the deadline now, the way hold's own dial always did -- and that is
# one function, hibr_uconn, which both use.
#
# The listener fills its own queue, and accepts half a second after the
# shell says it is about to connect:
# a connect that waits gets in, one that gives up at once does not. A
# listener that never accepts still ends the wait at the deadline.
command -v python3 > /dev/null 2>&1 || {
	echo "a full queue that drains: connected"
	echo "one that never does: refused after waiting"
	exit 0
}
d=$(mktemp -d) || exit 1
python3 - "$d" > /dev/null 2>&1 <<'E' &
import os, socket, sys, time

d = sys.argv[1]
def fill(path):
    s = socket.socket(socket.AF_UNIX)
    s.bind(path)
    s.listen(0)
    held = []
    while True:
        c = socket.socket(socket.AF_UNIX)
        c.setblocking(False)
        try:
            c.connect(path)
        except BlockingIOError:
            c.close()
            break
        held.append(c)
    return s, held

a, ha = fill(os.path.join(d, "a"))
b, hb = fill(os.path.join(d, "b"))
open(os.path.join(d, "ready"), "w").write("1\n")
end = time.time() + 10
while not os.path.exists(os.path.join(d, "go")) and time.time() < end:
    time.sleep(0.01)
time.sleep(0.5)
a.settimeout(0.2)
end = time.time() + 10
while time.time() < end:
    try:
        c, _ = a.accept()
        c.sendall(b"hello\n")
    except OSError:
        pass
E
server=$!
i=0
while [ "$i" -lt 100 ] && [ ! -f "$d/ready" ]; do
	sys sleepms 50 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
r=
: > "$d/go"
HIBR_CONNTIMEO=4000 "$HIBR" -c '
exec {fd}<>/dev/unix/'"$d"'/a || exit 1
read -t 4 -r l <&$fd
[ "$l" = hello ] && echo connected' > "$d/out" 2> /dev/null
read -r r < "$d/out"
echo "a full queue that drains: ${r:-gave up at once}"
t0=$EPOCHREALTIME
HIBR_CONNTIMEO=600 "$HIBR" -c 'exec {fd}<>/dev/unix/'"$d"'/b' 2> /dev/null
st=$?
t1=$EPOCHREALTIME
ms=$(( (${t1/./} - ${t0/./}) / 1000 ))
if [ "$st" != 0 ] && [ "$ms" -ge 500 ]; then
	echo "one that never does: refused after waiting"
else
	echo "one that never does: status $st after $ms ms"
fi
kill "$server" 2> /dev/null
wait "$server" 2> /dev/null
rm -rf "$d"
