# Networking

Sockets are file descriptors, so they work with everything else.

```
exec 3<>/dev/tcp/127.0.0.1/6379       # also /dev/udp/, /dev/unix/, /dev/tls/
printf 'PING\r\n' >&3; read pong <&3

connect host port sock      # -u UDP, -s TLS; sets $sock and $RET
send [-n|-r] $sock text…    # -r ends with CRLF
recv [-a|-n bytes] $sock v  # one line by default
accept $listenfd conn       # sets $conn and $REMOTE
listen [-f] [-n count] port handler
```

`listen` calls the handler once per connection with the socket as its standard
input and output, inside the shell, so the handler can use your functions and
variables; `-f` forks per connection instead.

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

---

[← documentation index](README.md) · [← project README](../README.md)
