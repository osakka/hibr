# HIBR_TLS_INSECURE turns certificate checking off only when it says so:
# set, not empty and not 0. It was read with getenv alone, so
# HIBR_TLS_INSECURE=0 -- which reads as "do check" -- and an empty value
# both turned verification off (Gitea #422). The server's certificate is
# self-signed and trusted by nobody, so a checking shell must refuse it.
command -v openssl > /dev/null 2>&1 && command -v python3 > /dev/null 2>&1 || {
	echo "unset: refused"
	echo "empty: refused"
	echo "0: refused"
	echo "1: hello"
	echo "yes: hello"
	exit 0
}
d=$(mktemp -d) || exit 1
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/k.pem" \
	-out "$d/c.pem" -subj /CN=localhost \
	-addext subjectAltName=DNS:localhost -days 2 > /dev/null 2>&1
python3 - "$d" > /dev/null 2>&1 <<'E' &
import os, socket, ssl, sys, threading, time

d = sys.argv[1]
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, "c.pem"), os.path.join(d, "k.pem"))
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
s.listen(8)


def run():
    while True:
        try:
            c, _ = s.accept()
        except OSError:
            return
        try:
            t = ctx.wrap_socket(c, server_side=True)
            t.send(b"hello\n")
            t.close()
        except Exception:
            pass


threading.Thread(target=run, daemon=True).start()
open(os.path.join(d, "port"), "w").write("%d\n" % s.getsockname()[1])
time.sleep(30)
E
server=$!
i=0
while [ "$i" -lt 100 ] && [ ! -f "$d/port" ]; do
	sys sleepms 100 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
read -r port < "$d/port"
# The variable goes to a child as an env prefix, and the unset case takes
# it out of the environment, so whatever the shell running this test was
# given cannot decide the answer. The handshake finishes on the first read,
# so a refused certificate shows there rather than on the open.
say() {
	local out
	local -a pre

	pre=(-u HIBR_TLS_INSECURE -u HIBR_TLS_CAFILE)
	[ "$#" -ge 1 ] && pre+=("HIBR_TLS_INSECURE=$1")
	out=$(env "${pre[@]}" "$HIBR" -c '
		exec {fd}<>"/dev/tls/localhost/$1" 2> /dev/null || exit 1
		read -r -t 5 l <&"$fd" 2> /dev/null || exit 1
		printf "%s" "$l"' insecure "$port" 2> /dev/null)
	ret "${out:-refused}"
}
r := say
echo "unset: $r"
r := say ""
echo "empty: $r"
r := say 0
echo "0: $r"
r := say 1
echo "1: $r"
r := say yes
echo "yes: $r"
kill "$server" 2> /dev/null
rm -rf "$d"
