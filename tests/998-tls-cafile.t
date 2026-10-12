# HIBR_TLS_CAFILE names a certificate authority to trust besides the ones
# the system already trusts. Before it, somebody with their own CA had two
# ways to reach their own server and no third: change the machine's trust
# store, which is root's and every program's business, or turn verification
# off altogether (Gitea #191).
#
# What makes it a variable rather than a line of documentation is the third
# check. SSL_CERT_FILE, which OpenSSL reads inside
# SSL_CTX_set_default_verify_paths, *replaces* the default CA file -- so on
# a box whose public roots live only in that file, naming a private CA that
# way silently stops every public certificate verifying. This adds a
# location to the same store instead, and two CAs with a server each is the
# only arrangement that can tell those two apart.
command -v openssl > /dev/null 2>&1 && command -v python3 > /dev/null 2>&1 || {
	echo "signed by the named CA: hello"
	echo "signed by the other, not named: refused"
	echo "one named each way: hello hello"
	echo "named empty: refused"
	exit 0
}
d=$(mktemp -d) || exit 1
for n in a b; do
	openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/k$n.pem" \
		-out "$d/c$n.pem" -subj /CN=localhost \
		-addext subjectAltName=DNS:localhost -days 2 > /dev/null 2>&1
done
python3 - "$d" > /dev/null 2>&1 <<'E' &
import os, socket, ssl, sys, threading, time

d = sys.argv[1]
ports = []
for cert, key in (("ca.pem", "ka.pem"), ("cb.pem", "kb.pem")):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(d, cert), os.path.join(d, key))
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen(4)
    ports.append(s.getsockname()[1])

    def run(s=s, ctx=ctx):
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
open(os.path.join(d, "ports"), "w").write("%d %d\n" % tuple(ports))
time.sleep(30)
E
server=$!
i=0
while [ "$i" -lt 100 ] && [ ! -f "$d/ports" ]; do
	sys sleepms 100 2> /dev/null || /bin/sleep 1
	i=$((i + 1))
done
read -r pa pb < "$d/ports"
# The handshake finishes on the first read, not on the open, so a
# connection that is about to fail verification still opens -- believing
# the open is how a refused certificate reads as a good one.
# The variables go to a child as an env prefix rather than being exported
# here: HIBR_TLS_INSECURE's own rule is that anything set turns
# verification off, and a test shell that declared one of these at file
# level would carry it into everything after.
say() {
	local port=$1 out
	local -a pre

	pre=()
	[ "$#" -ge 2 ] && pre+=("HIBR_TLS_CAFILE=$2")
	[ "$#" -ge 3 ] && pre+=("SSL_CERT_FILE=$3")
	out=$(env "${pre[@]}" "$HIBR" -c '
		exec {fd}<>"/dev/tls/localhost/$1" 2> /dev/null || exit 1
		read -r -t 5 l <&"$fd" 2> /dev/null || exit 1
		printf "%s" "$l"' cafile "$port" 2> /dev/null)
	ret "${out:-refused}"
}
a := say "$pa" "$d/ca.pem"
echo "signed by the named CA: $a"
b := say "$pb" "$d/ca.pem"
echo "signed by the other, not named: $b"
# Both, one through each mechanism: SSL_CERT_FILE replaces the default file
# and HIBR_TLS_CAFILE adds to it, so a shell given one of each trusts both
# servers. Either variable alone trusts exactly one of them.
a := say "$pa" "$d/cb.pem" "$d/ca.pem"
b := say "$pb" "$d/cb.pem" "$d/ca.pem"
echo "one named each way: $a $b"
# Empty is unset, which HIBR_TLS_INSECURE is not: anything set at all turns
# verification off there, and repeating that here would be a trap.
a := say "$pa" ""
echo "named empty: $a"
kill "$server" 2> /dev/null
rm -rf "$d"
