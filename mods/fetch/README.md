# fetch — an HTTP client for scripts and modules

`fetch` is HTTP/1.1 written here, no curl and no libraries: kept
connections, chunked and closed bodies, gzip and deflate, redirects,
Basic and Digest logins, proxies, cookies, and TLS through the shell's own
relay (libssl `dlopen`ed on first use, certificates verified). It is one
module with two faces -- a **builtin** for scripts, and an **interface**,
`"fetch"` version 1 (`mods/fetch.h`), for another module to make requests
through rather than carry an HTTP client of its own (Gitea #399).
The dav module and the `/dev/http` scheme are built on it, and so are the
browser's downloads.

The name is FreeBSD's `fetch(1)` as well. Loading the module makes the
builtin shadow a program of that name, if there is one.

```text
mod load fetch                      # or need fetch
fetch https://example.com/          # the body, to standard output
r := fetch https://example.com/     # the reply as a map instead
echo "${r["status"]} ${r["type"]} ${r["headers"]["content-length"]}"
fetch post https://api.example.com/x -H 'Content-Type: application/json' -d '{"a":1}'
fetch put https://host/file -f report.pdf
fetch https://host/big.iso -o big.iso
fetch -j work https://intranet/login -d 'user=me'   # cookies kept in jar "work"
fetch -u me:secret https://host/private
fetch jar clear work
```

## The builtin

`fetch [METHOD] URL [option]...`. The method is `get` (the default),
`head`, `post`, `put`, `patch`, `delete` or `options`, in any case.

| option | does |
|---|---|
| `-H 'Name: value'` | a header of the caller's, any number of them |
| `-d BODY`, `-f FILE` | a body from the word, or from a file -- read as it is sent, so a large one is never held in memory |
| `-o FILE` | the body written to a file rather than printed or kept |
| `-j NAME` | the cookie jar to send from and keep into; without one no cookie is sent or kept |
| `-u USER:PASS` | a login, Basic or Digest, whichever the server asks for, and sent only once it asks |
| `-t SECONDS` | how long without progress before giving up; 30 by default, or `HIBR_FETCH_TIMEOUT` |
| `-k` | do not check the certificate, for this request (`HIBR_TLS_INSECURE=1` for every one) |
| `-n` | hand a redirect back rather than follow it |
| `-i` | ask for the body uncompressed |

`FETCH_STATUS` is the reply's status, or 0 when no reply came at all. The
status of the command is 0 for a reply below 400, 1 for one at or above it
or for a request that failed, and 2 for a usage error; a failure says why
on standard error -- `fetch: http://127.0.0.1:9/x: cannot reach
127.0.0.1: Connection refused`.

With `:=` nothing is printed and the result is a map: `status`, `reason`,
`url` (where the reply came from, after redirects), `type`, `redirects`,
`length`, `error` when there was one, `body` (unless `-o`), and `headers`
by lowercased name -- a header sent more than once joined with `, ` as
HTTP allows, and `set-cookie`, which may not be, with a newline.

In an interactive shell ctrl-c stops a request that is waiting.

A desktop file under `strict vars` declares `FETCH_STATUS` at file
level, as it does any variable a module sets for it.

## What it does on the wire

- **Connections** are kept per scheme, host and port for the life of the
  process, and one that the server has closed meanwhile is opened again
  and the request sent once more. Four requests to one server from one
  shell are one connection.
- **Bodies** arrive with a length, chunked, or ended by the connection
  closing. One larger than 256 MB is refused when it is being kept in
  memory; written to a file or handed to a module's `ondata` it has no
  limit -- unless it is compressed, since a compressed body is held whole
  and decoded once it has all arrived, so it has the same limit and
  reaches `ondata` in one piece at the end. `-i` (`FT_IDENTITY`) is how a
  large download streams.
- **Compression**: `gzip` and `deflate` are offered unless `-i`, and
  decoded with the shell's own inflate (`mods/inflate.c`); a `deflate`
  body is read as zlib and, when that fails, as the raw stream some
  servers send under that name.
- **Redirects** are followed up to ten. A relative `Location` is resolved
  as RFC 3986 says, dot segments and all. 303 turns any request into a
  GET, and so do 301 and 302 for a POST, as browsers do; 307 and 308 send
  the same request again, body included -- a body from a file is read
  again from where it started. **On a redirect to another origin** the
  caller's own `Authorization`, `Cookie` and `Proxy-Authorization` are
  dropped, and the login is not offered there.
- **Headers** of the caller's own go out as given, except `Host`,
  `Content-Length` and `Transfer-Encoding`, which are fetch's.
- **Proxies** come from `http_proxy`, `https_proxy` and `no_proxy` (or
  their capitals); an `https` address goes through one with `CONNECT`.

## Cookies

A jar is a profile, named, so a script's cookies and a web app's are not
each other's: `$XDG_DATA_HOME/hibr/cookies/NAME.json` (by default
`~/.local/share/hibr/cookies/`), the file `0600` and the folder `0700`. It
is JSON, one object per cookie, so it can be read and edited by anything.
The rules are RFC 6265's: a cookie belongs to its host, and to the domain it
names only when the host is within it; `Path` matches by segment and a
missing one defaults to the request's folder; `Max-Age` wins over
`Expires`, and an expiry in the past removes the cookie; a `Secure` cookie
is neither set nor sent over plain `http`. They are sent longest path
first.

Three things to know:

- **There is no public suffix list**, so a server can set a cookie for a
  domain as wide as `example.co.uk` and the jar will keep it. A domain
  with no dot at all (`com`) is refused.
- **Session cookies are kept** in the file like any other, since a script
  has no session that ends; `fetch jar clear NAME` forgets them.
- **Several processes may share a jar.** Each reads the file again when it
  has changed, and a save takes the file as it is now, applies only what
  this process changed -- removals among them -- and renames a new file
  over it, so two writers do not lose each other's cookies.

## The interface

A module asks for it, and uses no symbol of this one's. The header is
installed as `include/hibr/fetch.h`, beside `hibr.h`, which it includes:

```c
#include "fetch.h"

ft_api *api = hibr_require(s, "fetch", FT_API_VER);
ft_req rq;
ft_res rs;

api->reqinit(&rq);                  /* every field its default */
rq.url = "https://example.com/";
rq.jar = "default";                 /* 0 for no cookies */
rq.ondata = my_sink;                /* or 0, and the body is in rs.body */
rq.cancel = &stop;                  /* set from anywhere to stop */
if (api->fetch(0, &rq, &rs) != 0)
	lg(HIBR_LERR, "%s", rs.err);
const char *cd = api->header(&rs, "Content-Disposition");
api->resfree(&rs);
```

`onhead` is called once with the final reply's status and headers, before
its body; `ondata` with each piece of the body, decoded unless
`FT_IDENTITY`. Either returning nonzero stops the request. Every wait is
cut into `FT_TICK` (250 ms) slices, and `*cancel` and the timeout are
looked at between them, so a cancel takes effect within a quarter of a
second; `rs.err` is then `cancelled`.

**Call it from a child process, not a thread.** TLS forks the shell's
relay, and the connection pool is one per process with no lock. A module
that wants several requests at once forks a worker for each; each worker
has its own connections, and the jar is safe across them, as above.

Settings come from the environment, so they reach a module's worker the
same way: `HIBR_FETCH_TIMEOUT`, `XDG_DATA_HOME`, the proxy variables and
`HIBR_TLS_INSECURE`.

## Files

| file | holds |
|---|---|
| `fetch.c` | the builtin, the interface table, `init` and `fini` |
| `http.c` | connections, the request and its reply, redirects, proxies, codings |
| `url.c` | addresses: split, written, resolved, compared; HTTP dates; base64 |
| `cookie.c` | jars: read, matched, set, merged and written |
| `auth.c`, `md5.c` | Basic and Digest, from the dav module's |
| `ft.h` | what the files share |
| `../fetch.h` | the public interface |

## Tests

`tests/fetch.py` runs the builtin and the interface against
`tests/fetchserve.py`, a stand-in that refuses what a careless client would
send -- a compressed reply to a client that did not offer it, a login with
the wrong answer, a Digest nonce it never issued -- on two ports, since two ports are
two origins. `tests/fetchapi.c` is a small module that drives the interface
with a streamed body and a cancel set from a timer.
