# Networking

Sockets are file descriptors, so they work with everything else.

```sh
exec 3<>/dev/tcp/127.0.0.1/6379
printf 'PING\r\n' >&3; read pong <&3
```

### Schemes

A scheme works anywhere a filename does — in a redirection, as an argument to
anything that opens a file.

| scheme | opens |
|---|---|
| `/dev/tcp/host/port` | a TCP connection |
| `/dev/udp/host/port` | a UDP socket |
| `/dev/tls/host/port` | a TLS session, certificate verified |
| `/dev/unix/path` | a Unix domain socket |
| `/dev/<name>/…` | whatever a module registered — the `http` module adds `/dev/http/` |

A scheme that cannot open says why and fails, like any other redirection:

```
$ hibr -c 'exec 3<>/dev/unix/nosuchsock'
hibr: connect /nosuchsock: No such file or directory
```

### The verbs

| form | does |
|---|---|
| `connect [-u\|-s] host port [var]` | connect; `-u` UDP, `-s` TLS. Sets `$var` (default `FD`) and `$RET` |
| `send [-n\|-r] fd text…` | write; `-n` without a trailing newline, `-r` ending CRLF |
| `recv [-a\|-n bytes] fd [var]` | read one line; `-a` everything to end of stream, `-n` exactly that many bytes |
| `accept listenfd [var]` | wait for one connection; sets `$var` (default `FD`) and `$REMOTE` |
| `listen [-f] [-n count] port handler` | serve; `-f` forks per connection, `-n` stops after that many |
| `listen -b port [var]` | bind only, and hand back the descriptor |

Descriptors come back above 9, so a redirection cannot tread on one. A failure
says what went wrong and returns non-zero:

```
$ hibr -c 'connect 127.0.0.1 1 S'
hibr: connect 127.0.0.1:1: Connection refused
```

`$REMOTE` is the peer, as `host:port` — `127.0.0.1:39254`.

`listen` calls the handler once per connection with the socket as its standard
input and output, inside the shell, so the handler can use your functions and
variables; `-f` forks per connection instead.

### A server and a client, whole

```sh
port=19700
listen -b $port LFD

srv() {
  accept $LFD C
  echo "server sees REMOTE=$REMOTE"
  recv $C line
  send $C "echo:$line"
  recv -n 4 $C four          # exactly four bytes
  send $C "got4:$four"
}
srv &
sleep 0.4

connect 127.0.0.1 $port S
send $S "hello";        recv $S r1; echo "client: $r1"
send -n $S "abcdefgh";  recv $S r2; echo "client: $r2"
wait
```

```
server sees REMOTE=127.0.0.1:39254
client: echo:hello
client: got4:abcd
```

Two processes, one shell, no external program.

`-b` binds and returns instead of serving, which is what lets a daemon stop
being root. Only opening the port needs privilege, so bind first, give it up,
and then serve — the descriptor outlives the privilege that opened it:

```
mod load sys
listen -b 80 LFD
drop www-data
while accept $LFD C; do serve <&$C >&$C; exec {C}<&-; done
```

A coprocess is reached with the same two verbs, because it is the same shape of
thing — see [0018](adr/0018-a-coprocess-is-an-endpoint.md):

```
worker() { while recv 0 line; do send 1 "got:$line"; done; }
coproc cp worker
send ${cp[out]} hello
recv ${cp[in]} answer        # ${cp[0]} and ${cp[1]} read the same, for bash
```

`drop user[:group]` comes from the `sys` module and gives up root for good: it
replaces the supplementary groups, then sets the real, effective **and saved**
ids, then checks its own work — the ids must read back as asked and `setuid(0)`
must fail. If anything fails once a change has been made it ends the shell
rather than carry on half dropped. See
[0016](adr/0016-privileges-are-dropped-never-gained.md).

The other shape, when the parent may stay root, is `listen -f`: it forks before
running the handler, so the handler drops and each connection is served
unprivileged while the parent keeps accepting.

An HTTPS request, parsed, with no external tools:

```
exec 3<>/dev/tls/api.example.com/443
send -r 3 "GET /v1/services HTTP/1.0"; send -r 3 "Host: api.example.com"; send -r 3 ""
recv 3 status
[[ $status =~ ^HTTP/1\.[01]\ ([0-9]{3}) ]] && echo "HTTP ${M[1]}"
while recv 3 h; do [[ -z $h ]] && break; done
recv -a 3 body
json parse api "$body"
```

**TLS** hands the shell an ordinary descriptor backed by a relay process that
does the handshake, which is why `read` and `>&3` work unchanged. Certificates
and hostnames are verified by default; `HIBR_TLS_INSECURE=1` turns that off.
libssl is loaded with `dlopen` on the first TLS connection, so a shell that
never uses TLS pays nothing for it — linking it would have cost 1.6 MB of
memory in every shell. No OpenSSL headers are needed to build.

## What this replaces

A shell that can open a socket, speak TLS, parse the reply as JSON and serve a
port needs no `curl`, no `nc`, no `jq` and no `socat` for most of what those get
used for. [`examples/httpd.hibr`](../examples/httpd.hibr) is a static web server
in eighty lines that forks for nothing, and
[`examples/fetch.hibr`](../examples/fetch.hibr) is an HTTP client with declared
options. More end-to-end recipes are in the [cookbook](cookbook.md).

---

[← documentation index](README.md) · [← project README](../README.md)
