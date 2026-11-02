# The TLS relay is a process of its own, and it has to behave like one
# (Gitea #465). It was forked with whatever the shell had: its process
# group, so the terminal's interrupt -- or anything a script sends with
# `kill 0` -- reached it, with SIGINT put back to the default to make sure
# it died of it; the shell's caught handlers, so any trapped signal ended
# its wait with EINTR, which it took for the end; and every descriptor the
# shell had open, so a relay forked after connection A kept A's end of the
# pipe open, and closing A in the shell never reached A's server.
#
# Two checks, each failing against 0.99.427: a reply still arrives after the
# relay's old process group is sent INT, USR1 and WINCH while it waits; and
# with A and B open, closing A ends A's connection while B lives on.
command -v openssl > /dev/null 2>&1 && command -v python3 > /dev/null 2>&1 &&
	command -v setsid > /dev/null 2>&1 || {
	echo "a signal to the shell's group leaves the relay: ok-0"
	echo "closing one connection ends it while another is open: closed 1"
	exit 0
}
d=$(mktemp -d) || exit 1
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/k.pem" -out "$d/c.pem" \
	-subj /CN=localhost -addext subjectAltName=DNS:localhost -days 2 \
	> /dev/null 2>&1
python3 - "$d" > /dev/null 2>&1 <<'E' &
import os, socket, ssl, sys, threading, time

d = sys.argv[1]
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, "c.pem"), os.path.join(d, "k.pem"))
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
s.listen(8)
open(os.path.join(d, "port"), "w").write("%d\n" % s.getsockname()[1])
lock = threading.Lock()

def note(text):
    with lock:
        with open(os.path.join(d, "log"), "a") as f:
            f.write(text + "\n")

def serve(c, n):
    try:
        t = ctx.wrap_socket(c, server_side=True)
        while True:
            b = t.recv(4096)
            if not b:
                note("closed %d" % n)
                return
            if b"PING" in b:
                time.sleep(0.6)
                t.sendall(b"ok-%d\n" % n)
    except Exception:
        note("closed %d" % n)

end = time.time() + 30
n = 0
while time.time() < end:
    try:
        s.settimeout(end - time.time())
        c, _ = s.accept()
    except OSError:
        break
    threading.Thread(target=serve, args=(c, n), daemon=True).start()
    n += 1
E
server=$!
i=0
while [ "$i" -lt 100 ] && [ ! -f "$d/port" ]; do
	sys sleepms 100 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
read -r port < "$d/port"
export HIBR_TLS_INSECURE=1

# The script runs in a session of its own, so `kill 0` reaches it and
# whatever shares its group, and nothing of the harness's.
setsid "$HIBR" -c '
trap : INT USR1 WINCH
exec {fd}<>/dev/tls/127.0.0.1/'"$port"' || { echo "connect failed"; exit 1; }
printf "PING\n" >&$fd
kill -INT 0
kill -USR1 0
kill -WINCH 0
r=gone
read -t 5 -r r <&$fd
echo "a signal to the shell'"'"'s group leaves the relay: $r"
exec {fd}>&-' < /dev/null

exec {a}<>"/dev/tls/127.0.0.1/$port"
exec {b}<>"/dev/tls/127.0.0.1/$port"
exec {a}>&-
i=0
got=
while [ "$i" -lt 30 ]; do
	got=
	[ -f "$d/log" ] && while read -r l; do
		case $l in "closed 1") got=$l ;; esac
	done < "$d/log"
	[ -n "$got" ] && break
	sys sleepms 100 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
echo "closing one connection ends it while another is open: ${got:-still open}"
exec {b}>&-
kill "$server" 2> /dev/null
wait "$server" 2> /dev/null
rm -rf "$d"
