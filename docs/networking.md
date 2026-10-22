# Networking

Sockets are file descriptors, so they work with everything else.

<!-- not run: needs a Redis server on port 6379 -->
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

```sh
exec 3<>/dev/unix/nosuchsock
```

```output
hibr: connect /nosuchsock: No such file or directory
```

### The verbs

| form | does |
|---|---|
| `connect [-u\|-s] host port [var]` | connect; `-u` UDP, `-s` TLS. Sets `$var` (default `FD`) and `$RET` |
| `send [-n\|-r] fd text…` | write; `-n` without a trailing newline, `-r` ending CRLF |
| `recv [-a\|-n bytes] fd [var]` | read one line; `-a` everything to end of stream, `-n` exactly that many bytes |
| `accept [-t secs] listenfd [var]` | wait for one connection; sets `$var` (default `FD`) and `$REMOTE`. `-t` waits at most that long and fails if none came; `-t 0` only looks |
| `listen [-f] [-n count] port handler` | serve; `-f` forks per connection, `-n` stops after that many |
| `listen -b [-u] port [var]` | bind only, and hand back the descriptor; `-u` binds UDP |

A port that is a path -- anything with a `/` in it -- is a Unix domain
socket: made so only its owner can open it, one left by a process that has
gone replaced, one still answering refused as in use. `$REMOTE` for a Unix
socket's connection is `uid:N`, the connecting process's user, so a server
can refuse anyone but its own: the desktop's control socket does.
`exec {fd}<>/dev/unix/path` (or `/dev/tcp/...`) puts the connection's
descriptor in `fd`, as it does a file's.

**`HIBR_CONNTIMEO` bounds a connect**, in milliseconds, and reaches every
connect this shell makes -- `/dev/tcp`, `/dev/udp`, `/dev/unix`, `/dev/tls`
and the `connect` builtin. Unset, a connect waits as long as the kernel
makes it, which is what it has always done and what a script that waits on
purpose needs; set, it gives up and says so. The case it is for is a socket
that is **listening with a full accept queue and nobody accepting** -- a
server wedged mid-request, where the connection is neither refused nor
completed -- which `desktop ctl` hit and now sets this for itself. The
value is a whole number of milliseconds: `0` or less means no bound, more
than an hour is an hour, and anything else -- `5s`, say -- is ignored, as
though it were unset. `hold` reads it the same way, with a bound of its own
when it is unset. A refused connect still says refused:

```sh
HIBR_CONNTIMEO=200 exec {fd}<>/dev/tcp/127.0.0.1/9; echo "status $?"
```

```output
hibr: connect 127.0.0.1:9: Connection refused
status 1
```

`hold` reads the same variable and is the one place where **unset does not
mean unbounded**: a request to a session is a control request to a local
socket a moment away, and no caller of it waits on purpose, where a
`/dev/tcp` script may. So the dial and the reply each have a default
(1000 ms and 5000 ms), which this variable overrides for both. Before that
(0.99.171, Gitea #204), one session that was listening and not accepting
stopped everything that asked after it -- `hold list` dials *every* socket
in the directory, and a desktop runs it from `dt_autohold` before
`desktop.log` exists, so the session hung with nothing drawn and no error
anywhere (Gitea #170).

Descriptors come back above 9, so a redirection cannot tread on one. A failure
says what went wrong and returns non-zero, and a descriptor that is not a
number is refused rather than taken as 0:

```sh
connect 127.0.0.1 1 S
send "$unset" hello; echo "status $?"
```

```output
hibr: connect 127.0.0.1:1: Connection refused
hibr: : not a descriptor
status 2
```

`$REMOTE` is the peer, as `host:port` — `127.0.0.1:39254`.

`listen` calls the handler once per connection with the socket as its standard
input and output, inside the shell, so the handler can use your functions and
variables; `-f` forks per connection instead.

### A server and a client, whole

```sh
port=$(( 20000 + $$ % 20000 ))
listen -b $port LFD

srv() {
  accept $LFD C
  echo "server sees a client from ${REMOTE%:*}"
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

```output
server sees a client from 127.0.0.1
client: echo:hello
client: got4:abcd
```

Two processes, one shell, no external program.

`-b` binds and returns instead of serving, which is what lets a daemon stop
being root. Only opening the port needs privilege, so bind first, give it up,
and then serve — the descriptor outlives the privilege that opened it:

<!-- not run: needs root, and serves until stopped -->
```sh
mod load sys
listen -b 80 LFD
drop www-data
while accept $LFD C; do serve <&$C >&$C; exec {C}<&-; done
```

A coprocess is reached with the same two verbs, because it is the same shape of
thing — see [0018](adr/0018-a-coprocess-is-an-endpoint.md):

```sh
worker() { while recv 0 line; do send 1 "got:$line"; done; }
coproc cp worker
send ${cp[out]} hello
recv ${cp[in]} answer
echo "$answer"
send ${cp[1]} again          # ${cp[0]} and ${cp[1]} name the same ends, for bash
recv ${cp[0]} answer
echo "$answer"
```

```output
got:hello
got:again
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

<!-- not run: needs the public network -->
```sh
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

**Your own certificate authority.** `HIBR_TLS_CAFILE` names a file of
certificates to trust **besides** the ones the system already trusts, which is
what a self-hosted server signed by a private CA needs:

<!-- not run: it needs a server with a certificate signed by that authority -->
```sh
HIBR_TLS_CAFILE=/etc/ssl/certs/my-ca.crt desktop
```

It is read from the environment, so it reaches a module and a child alike —
the relay is a forked child, and the mail, WebDAV and vault modules all speak
through it. An empty value counts as unset.

It is not the same thing as OpenSSL's own `SSL_CERT_FILE`, which **replaces**
the default CA file: on a machine whose public roots live only in that file,
naming a private authority that way stops every public certificate verifying.
`HIBR_TLS_CAFILE` adds a location to the same store, so a private authority
costs nothing of the public ones. The alternative is to give the authority to
the machine itself (`update-ca-certificates` and its equivalents), which is
root's decision and every program's business; this is neither, and it is not
`HIBR_TLS_INSECURE`, which trusts nothing at all.

## What this replaces

A shell that can open a socket, speak TLS, parse the reply as JSON and serve a
port needs no `curl`, no `nc`, no `jq` and no `socat` for most of what those get
used for. [`examples/httpd.hibr`](../examples/httpd.hibr) is a static web server
in a little over a hundred lines that forks for nothing, and
[`examples/fetch.hibr`](../examples/fetch.hibr) is an HTTP client with declared
options. More end-to-end recipes are in the [cookbook](cookbook.md).

---

[← documentation index](README.md) · [← project README](../README.md)
