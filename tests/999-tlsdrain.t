# A TLS record carries up to 16 kB of plaintext and SSL_read answers at most
# what it was asked for, so the remainder stays in OpenSSL's own buffer --
# where select on the socket can never report it, because the bytes have
# already arrived. tls_relay read once per select, and the tail of a reply
# was therefore stranded for as long as the connection stayed open: a peer
# that **closes** hides it, since the next select fires on the end of file
# and the buffer drains by luck, and a kept connection does not (Gitea #223).
#
# Found by `apps`: the published catalogue grew to 87 kB and `dav get` waited
# its whole 30-second receive timeout for the last 1386 bytes, reporting a
# server that "did not answer" for a reply that was sitting in this process.
# It is size-dependent only in that the last record has to leave a remainder
# after a 4 kB read, which is why a 60 kB file and a 264 kB file both arrived
# and that one never did -- so the size here is deliberately not a multiple
# of 4096, and the reply is sent in one write with the connection kept open.
#
# 90067 of 90067 with the drain, 86016 of 90067 without it: the one number
# that matters is that this test fails against the release before it.
command -v openssl > /dev/null 2>&1 && command -v python3 > /dev/null 2>&1 || {
	echo "a kept connection's last record arrives: whole"
	exit 0
}
d=$(mktemp -d) || exit 1
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/k.pem" -out "$d/c.pem" \
	-subj /CN=localhost -addext subjectAltName=DNS:localhost -days 2 \
	> /dev/null 2>&1
python3 - "$d" > /dev/null 2>&1 <<'E' &
import os, socket, ssl, sys, time

d = sys.argv[1]
body = b"L" * 90001
head = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n" % len(body)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, "c.pem"), os.path.join(d, "k.pem"))
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
s.listen(4)
open(os.path.join(d, "port"), "w").write("%d\n" % s.getsockname()[1])
end = time.time() + 30
while time.time() < end:
    try:
        s.settimeout(end - time.time())
        c, _ = s.accept()
    except OSError:
        break
    try:
        t = ctx.wrap_socket(c, server_side=True)
        t.recv(4096)
        # One write, and the connection is **kept**: nothing after this
        # wakes a relay that only reads once per select.
        t.sendall(head + body)
        time.sleep(25)
    except Exception:
        pass
E
server=$!
i=0
while [ "$i" -lt 100 ] && [ ! -f "$d/port" ]; do
	sys sleepms 100 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
read -r port < "$d/port"
# The reader asks for exactly what is left rather than a fixed 4096: `read
# -N` takes exactly N characters and blocks on a short tail, which made the
# first version of this count 86016 whether the relay was fixed or not.
HIBR_TLS_INSECURE=1 "$HIBR" -c '
exec {fd}<>/dev/tls/127.0.0.1/'"$port"' || { echo "connect failed"; exit 1; }
printf "GET /big HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n" >&$fd
want=90067
n=0
while [ "$n" -lt "$want" ]; do
	left=$((want - n))
	[ "$left" -gt 4096 ] && left=4096
	read -t 3 -N "$left" c <&$fd || break
	n=$((n + ${#c}))
done
if [ "$n" = "$want" ]; then
	echo "a kept connection'"'"'s last record arrives: whole"
else
	echo "a kept connection'"'"'s last record arrives: $n of $want"
fi
exec {fd}>&-'
kill "$server" 2> /dev/null
wait "$server" 2> /dev/null
rm -rf "$d"
